// demucs.cpp (and Eigen) first, before JUCE's `using namespace juce`
#include "model.hpp"
#include "tensor.hpp"

#include "AI/StemSeparation.h"

namespace
{
    struct Cancelled {};

    const Stems::ModelInfo modelInfos[] = {
        { Stems::ModelType::sixStems,
          "6 stems: vocals, drums, bass, guitar, piano, other",
          "ggml-model-htdemucs-6s-f16.bin",
          "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/ggml-model-htdemucs-6s-f16.bin",
          { "drums", "bass", "other", "vocals", "guitar", "piano" },
          53 },
        { Stems::ModelType::fourStems,
          "4 stems: vocals, drums, bass, other (cleaner vocals)",
          "ggml-model-htdemucs-4s-f16.bin",
          "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/ggml-model-htdemucs-4s-f16.bin",
          { "drums", "bass", "other", "vocals" },
          81 },
    };

    bool isAscii (const String& s)
    {
        for (auto c : s)
            if ((juce_wchar) c >= 128)
                return false;

        return true;
    }

    /** Converts (part of) a sample to the stereo 44.1 kHz matrix that Demucs expects. */
    Eigen::MatrixXf toModelInput (const SampleData& input, Range<int> range)
    {
        const auto& b = input.buffer;
        const auto length = range.getLength();
        const auto ratio = input.sampleRate / Stems::modelSampleRate;
        const auto sameRate = std::abs (ratio - 1.0) < 1.0e-9;
        const auto outLength = sameRate ? length : (int) std::llround (length / ratio);

        Eigen::MatrixXf audio (2, outLength);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* src = b.getReadPointer (jmin (ch, b.getNumChannels() - 1), range.getStart());

            if (sameRate)
            {
                for (int i = 0; i < outLength; ++i)
                    audio (ch, i) = src[i];
            }
            else
            {
                // pad so the interpolator never reads past the end
                std::vector<float> in ((size_t) length + 512, 0.0f);
                std::copy (src, src + length, in.begin());
                std::vector<float> out ((size_t) outLength, 0.0f);
                WindowedSincInterpolator interpolator;
                interpolator.process (ratio, in.data(), out.data(), outLength, (int) in.size(), 0);

                for (int i = 0; i < outLength; ++i)
                    audio (ch, i) = out[(size_t) i];
            }
        }

        return audio;
    }

    /** Separates one block of stereo 44.1 kHz audio into (sources, 2, length). */
    using InferFn = std::function<Eigen::Tensor3dXf (const Eigen::MatrixXf&, const std::function<void (float)>& progress)>;

    /** One part of the song, separated on its own thread (demucs.cpp is single-threaded). */
    class SegmentWorker : public Thread
    {
    public:
        SegmentWorker (const InferFn& fn, Eigen::MatrixXf audio, int sources, std::function<void (float)> onProgress)
            : Thread ("Demucs segment", 16 * 1024 * 1024),
              infer (fn), segment (std::move (audio)), numSources (sources), progress (std::move (onProgress)) {}

        void run() override
        {
            try
            {
                // Demucs normalises by the standard deviation: skip silent parts (it would divide by 0)
                const Eigen::VectorXf mono = segment.colwise().mean();
                const auto mean = mono.mean();
                const auto variance = (mono.array() - mean).square().sum() / (float) jmax ((Eigen::Index) 1, mono.size() - 1);

                if (variance < 1.0e-14f)
                {
                    output = Eigen::Tensor3dXf (numSources, 2, (Eigen::Index) segment.cols());
                    output.setZero();
                    progress (1.0f);
                    ok = true;
                    return;
                }

                output = infer (segment, progress);
                ok = true;
            }
            catch (const Cancelled&)
            {
                cancelled = true;
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
            catch (...)
            {
                error = "unknown error";
            }
        }

        const InferFn& infer;
        Eigen::MatrixXf segment;
        const int numSources;
        std::function<void (float)> progress;

        Eigen::Tensor3dXf output;
        bool ok = false, cancelled = false;
        std::string error;
    };
}

namespace
{
    /** Splits the audio into overlapping parts, separates them in parallel and cross-fades the
        results back together. */
    Result runInParts (const Eigen::MatrixXf& audio, int numThreads, const StringArray& stemIds, const InferFn& infer,
                       const std::function<void (float, const String&)>& report, std::atomic<bool>& cancelled,
                       std::vector<Stems::Stem>& result)
    {
        using namespace Stems;
        const auto total = (int) audio.cols();
        const auto numSources = stemIds.size();
        // split the audio into parts processed in parallel, with overlapping edges.
        // Each part needs ~1.7 GB of RAM while it runs, so the RAM limits the parallelism too.
        if (numThreads <= 0)
        {
            const auto memoryMB = SystemStats::getMemorySizeInMegabytes();
            const auto byMemory = memoryMB > 0 ? (memoryMB / 2 - 500) / 1700 : 2;
            numThreads = jlimit (1, 8, jmin (SystemStats::getNumPhysicalCpus(), byMemory));
        }

        const auto minPart = (int) (modelSampleRate * 6.0);
        const auto numParts = jlimit (1, numThreads, jmax (1, total / minPart));
        const auto partLength = (total + numParts - 1) / numParts;
        const auto overlap = numParts > 1 ? (int) (modelSampleRate * 1.0) : 0;

        auto partProgress = std::make_unique<std::atomic<float>[]> ((size_t) numParts);
        std::vector<std::unique_ptr<SegmentWorker>> workers;
        std::vector<Range<int>> extents;   // part positions including overlap (may extend past the audio)

        for (int i = 0; i < numParts; ++i)
        {
            partProgress[(size_t) i] = 0.0f;
            const auto coreStart = i * partLength;
            const auto coreEnd = jmin (total, coreStart + partLength);
            const Range<int> extent (coreStart - overlap, coreEnd + overlap);
            extents.push_back (extent);

            Eigen::MatrixXf part = Eigen::MatrixXf::Zero (2, extent.getLength());
            const auto from = jmax (0, extent.getStart()), to = jmin (total, extent.getEnd());
            part.block (0, from - extent.getStart(), 2, to - from) = audio.block (0, from, 2, to - from);

            auto onProgress = [&, i] (float p)
            {
                partProgress[(size_t) i] = p;
                float sum = 0.0f;

                for (int k = 0; k < numParts; ++k)
                    sum += partProgress[(size_t) k].load();

                report (0.05f + 0.93f * sum / (float) numParts, "Separating with AI...");
            };

            workers.push_back (std::make_unique<SegmentWorker> (infer, std::move (part), numSources, onProgress));
        }

        for (auto& w : workers)
            w->startThread();

        for (auto& w : workers)
            w->waitForThreadToExit (-1);


        for (auto& w : workers)
        {
            if (w->cancelled || cancelled)
                return Result::fail ("Cancelled");

            if (! w->ok)
                return Result::fail ("AI separation failed: " + String (w->error));
        }

        // overlap-add with linear cross-fades between neighbouring parts, straight into the stems
        report (0.99f, "Finishing...");

        for (int st = 0; st < numSources; ++st)
        {
            auto stem = std::make_shared<SampleData>();
            stem->sampleRate = modelSampleRate;
            stem->buffer.setSize (2, total);
            stem->buffer.clear();
            result.push_back ({ stemIds[st], stem });
        }

        std::vector<float> weightSum ((size_t) total, 0.0f);
        const auto fade = jmax (1, 2 * overlap);

        for (int i = 0; i < numParts; ++i)
        {
            const auto& out = workers[(size_t) i]->output;
            const auto extent = extents[(size_t) i];

            for (int j = 0; j < extent.getLength(); ++j)
            {
                const auto t = extent.getStart() + j;

                if (t < 0 || t >= total)
                    continue;

                auto w = 1.0f;

                if (numParts > 1)
                    w = jlimit (1.0e-3f, 1.0f, jmin ((float) (j + 1) / (float) fade, (float) (extent.getLength() - j) / (float) fade));

                weightSum[(size_t) t] += w;

                for (int st = 0; st < numSources; ++st)
                    for (int ch = 0; ch < 2; ++ch)
                        result[(size_t) st].audio->buffer.addSample (ch, t, w * out (st, ch, j));
            }

            workers[(size_t) i].reset();   // free this part's output early
        }

        bool valid = true;

        for (auto& stem : result)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = stem.audio->buffer.getWritePointer (ch);

                for (int t = 0; t < total; ++t)
                {
                    d[t] /= weightSum[(size_t) t];
                    valid = valid && std::isfinite (d[t]) && std::abs (d[t]) < 1000.0f;
                }
            }

        if (! valid)
        {
            result.clear();
            return Result::fail ("The AI model produced invalid audio. The model file may be incomplete or "
                                 "not a Demucs v4 model; delete it and download it again.");
        }

        return Result::ok();
    }
}

//==============================================================================
const Stems::ModelInfo& Stems::getModelInfo (ModelType type)
{
    return modelInfos[type == ModelType::fourStems ? 1 : 0];
}

String Stems::getStemDisplayName (const String& id)
{
    if (id == "novocals")
        return "No vocals";

    return id.substring (0, 1).toUpperCase() + id.substring (1);
}

String Stems::getModelsPageUrl()
{
    return "https://huggingface.co/datasets/Retrobear/demucs.cpp/tree/main";
}

String Stems::getModelUrl (ModelType type)
{
    // MYDAW_MODEL_BASE_URL can point at a mirror (e.g. where Hugging Face is not reachable)
    auto base = SystemStats::getEnvironmentVariable ("MYDAW_MODEL_BASE_URL", {});

    if (base.isEmpty())
        return getModelInfo (type).url;

    return base.trimCharactersAtEnd ("/") + "/" + getModelInfo (type).fileName;
}

File Stems::getModelsFolder()
{
    auto folder = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("myDAW").getChildFile ("Models");

   #if JUCE_WINDOWS
    // demucs.cpp opens the model with fopen(), which cannot open non-ASCII paths on Windows
    // (e.g. a Japanese user name). C:\ProgramData is always ASCII.
    if (! isAscii (folder.getFullPathName()))
        folder = File::getSpecialLocation (File::commonApplicationDataDirectory).getChildFile ("myDAW").getChildFile ("Models");
   #else
    ignoreUnused (isAscii);
   #endif

    return folder;
}

File Stems::getModelFile (ModelType type)
{
    return getModelsFolder().getChildFile (getModelInfo (type).fileName);
}

bool Stems::isModelInstalled (ModelType type)
{
    return isValidModelFile (getModelFile (type));
}

bool Stems::isValidModelFile (const File& file, ModelType* type)
{
    if (! file.existsAsFile() || file.getSize() < 1024 * 1024)
        return false;

    FileInputStream in (file);

    if (! in.openedOk())
        return false;

    const auto magic = (uint32) in.readInt();   // little-endian, like the writer

    if (magic == 0x646d6336) { if (type != nullptr) *type = ModelType::sixStems;  return true; }  // "dmc6"
    if (magic == 0x646d6334) { if (type != nullptr) *type = ModelType::fourStems; return true; }  // "dmc4"

    return false;
}

Result Stems::installModelFile (const File& source, ModelType* typeOut)
{
    ModelType type;

    if (! isValidModelFile (source, &type))
        return Result::fail (source.getFileName() + " is not a Demucs v4 model file (ggml-model-htdemucs-...-f16.bin).");

    const auto target = getModelFile (type);

    if (source != target)
    {
        if (! target.getParentDirectory().createDirectory() || ! source.copyFileTo (target))
            return Result::fail ("Could not copy the model to " + target.getFullPathName());
    }

    if (typeOut != nullptr)
        *typeOut = type;

    return Result::ok();
}

bool Stems::selfTestPartMerging (String& details)
{
    // a fake "model": stem s = input * (s + 1). Any mistake in splitting, placing or
    // cross-fading the parts shows up as a difference from that.
    const auto total = (int) (modelSampleRate * 31.3);
    Eigen::MatrixXf audio (2, total);
    Random random (7);

    for (int i = 0; i < total; ++i)
    {
        audio (0, i) = random.nextFloat() - 0.5f;
        audio (1, i) = std::sin ((float) i * 0.01f);
    }

    audio.block (0, total / 3, 2, (int) modelSampleRate * 2).setZero();   // a silent stretch

    const StringArray ids { "a", "b", "c" };
    InferFn fake = [&ids] (const Eigen::MatrixXf& part, const std::function<void (float)>& progress)
    {
        Eigen::Tensor3dXf out (ids.size(), 2, (Eigen::Index) part.cols());

        for (int s = 0; s < ids.size(); ++s)
            for (int c = 0; c < 2; ++c)
                for (Eigen::Index i = 0; i < part.cols(); ++i)
                    out (s, c, i) = part (c, i) * (float) (s + 1);

        progress (1.0f);
        return out;
    };

    std::atomic<bool> cancelled { false };
    auto report = [] (float, const String&) {};

    for (int threads : { 1, 4 })
    {
        std::vector<Stem> stems;
        const auto r = runInParts (audio, threads, ids, fake, report, cancelled, stems);

        if (r.failed() || (int) stems.size() != ids.size())
        {
            details = "run failed: " + r.getErrorMessage();
            return false;
        }

        float worst = 0.0f;

        for (int s = 0; s < ids.size(); ++s)
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < total; ++i)
                    worst = jmax (worst, std::abs (stems[(size_t) s].audio->buffer.getSample (c, i) - audio (c, i) * (float) (s + 1)));

        details << threads << " thread(s): max error " << worst << "  ";

        if (worst > 1.0e-4f || stems[0].audio->getLength() != total)
            return false;
    }

    return true;
}

std::shared_ptr<SampleData> Stems::mixWithout (const std::vector<Stem>& stems, const String& excludedId)
{
    std::shared_ptr<SampleData> mix;

    for (auto& s : stems)
    {
        if (s.id == excludedId || s.audio == nullptr)
            continue;

        if (mix == nullptr)
        {
            mix = std::make_shared<SampleData>();
            mix->sampleRate = s.audio->sampleRate;
            mix->buffer.makeCopyOf (s.audio->buffer);
        }
        else
        {
            for (int ch = 0; ch < mix->buffer.getNumChannels(); ++ch)
                mix->buffer.addFrom (ch, 0, s.audio->buffer, jmin (ch, s.audio->buffer.getNumChannels() - 1), 0,
                                     jmin (mix->getLength(), s.audio->getLength()));
        }
    }

    return mix;
}

Result Stems::separate (const File& modelFile, const SampleData& input, Range<int> range, int numThreads,
                        std::vector<Stem>& result, ProgressFn progressFn)
{
    result.clear();
    std::atomic<bool> cancelled { false };

    auto report = [&] (float p, const String& status)
    {
        if (progressFn && ! progressFn (p, status))
        {
            cancelled = true;
            throw Cancelled();
        }
    };

    try
    {
        range = Range<int> (0, input.getLength()).getIntersectionWith (range);

        if (range.isEmpty())
            range = { 0, input.getLength() };

        if (range.getLength() < (int) (input.sampleRate * 0.5))
            return Result::fail ("The audio is too short to separate (at least 0.5 seconds are needed).");

        ModelType type;

        if (! isValidModelFile (modelFile, &type))
            return Result::fail ("The AI model is missing or damaged: " + modelFile.getFullPathName());

        report (0.0f, "Loading the AI model...");

        auto model = std::make_unique<demucscpp::demucs_model>();

        if (! demucscpp::load_demucs_model (modelFile.getFullPathName().toStdString(), model.get()))
            return Result::fail ("Could not load the AI model " + modelFile.getFileName());

        const auto& info = getModelInfo (model->is_4sources ? ModelType::fourStems : ModelType::sixStems);

        report (0.03f, "Preparing the audio...");
        const auto audio = toModelInput (input, range);

        auto infer = [&model] (const Eigen::MatrixXf& part, const std::function<void (float)>& progress)
        {
            return demucscpp::demucs_inference (*model, part, [&progress] (float p, const std::string&) { progress (p); });
        };

        if (const auto r = runInParts (audio, numThreads, info.stems, infer, report, cancelled, result); r.failed())
            return r;

        report (1.0f, "Done");
        return Result::ok();
    }
    catch (const Cancelled&)
    {
        return Result::fail ("Cancelled");
    }
    catch (const std::bad_alloc&)
    {
        return Result::fail ("Not enough memory. Try separating a shorter selection.");
    }
    catch (const std::exception& e)
    {
        return Result::fail (String ("AI separation failed: ") + e.what());
    }
}

//==============================================================================
ModelDownloadJob::ModelDownloadJob (Stems::ModelType t) : Thread ("Model download"), type (t) {}

ModelDownloadJob::~ModelDownloadJob()
{
    stopThread (10000);
}

String ModelDownloadJob::getError() const
{
    const ScopedLock sl (lock);
    return error;
}

void ModelDownloadJob::run()
{
    auto fail = [this] (const String& message)
    {
        const ScopedLock sl (lock);
        error = message;
    };

    const auto target = Stems::getModelFile (type);

    if (! target.getParentDirectory().createDirectory())
    {
        fail ("Cannot create " + target.getParentDirectory().getFullPathName());
        finished = true;
        return;
    }

    int statusCode = 0;
    auto stream = URL (Stems::getModelUrl (type)).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                        .withConnectionTimeoutMs (20000)
                                                        .withNumRedirectsToFollow (10)
                                                        .withStatusCode (&statusCode));

    if (stream == nullptr || (statusCode != 0 && statusCode != 200))
    {
        fail ("Could not download the AI model (" + (statusCode != 0 ? "HTTP " + String (statusCode) : String ("no connection"))
              + ").\nYou can download it in your browser from\n" + Stems::getModelsPageUrl()
              + "\nand load it with \"Load model file...\".");
        finished = true;
        return;
    }

    const auto totalLength = stream->getTotalLength();
    TemporaryFile temp (target);

    {
        FileOutputStream out (temp.getFile());

        if (! out.openedOk())
        {
            fail ("Cannot write " + temp.getFile().getFullPathName());
            finished = true;
            return;
        }

        HeapBlock<char> chunk (65536);
        int64 done = 0;

        while (! threadShouldExit())
        {
            const auto n = stream->read (chunk, 65536);

            if (n <= 0)
                break;

            out.write (chunk, (size_t) n);
            done += n;

            if (totalLength > 0)
                progress = (double) done / (double) totalLength;
        }

        if (threadShouldExit())
        {
            fail ("Cancelled");
            finished = true;
            return;
        }
    }

    if (! Stems::isValidModelFile (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
        fail ("The downloaded file is not a valid model. Please try again, or download it in your browser from\n"
              + Stems::getModelsPageUrl());

    progress = 1.0;
    finished = true;
}

//==============================================================================
StemSeparationJob::StemSeparationJob (const File& model, std::shared_ptr<const SampleData> in, Range<int> r)
    : Thread ("Stem separation", 16 * 1024 * 1024), modelFile (model), input (std::move (in)), range (r)
{
}

StemSeparationJob::~StemSeparationJob()
{
    cancel();
    stopThread (60000);
}

String StemSeparationJob::getStatus() const
{
    const ScopedLock sl (lock);
    return status;
}

String StemSeparationJob::getError() const
{
    const ScopedLock sl (lock);
    return error;
}

void StemSeparationJob::run()
{
    const auto result = Stems::separate (modelFile, *input, range, 0, stems, [this] (float p, const String& s)
    {
        progress = p;

        {
            const ScopedLock sl (lock);
            status = s;
        }

        return ! (cancelled.load() || threadShouldExit());
    });

    if (result.failed())
    {
        const ScopedLock sl (lock);
        error = result.getErrorMessage();
        stems.clear();
    }

    finished = true;
}
