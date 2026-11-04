#pragma once

#include "Audio/SampleData.h"

/** AI music source separation ("stems") with Meta's Demucs v4 hybrid-transformer model,
    running locally on the CPU through demucs.cpp. */
namespace Stems
{
    enum class ModelType { sixStems = 0, fourStems = 1 };

    struct ModelInfo
    {
        ModelType type;
        String displayName;
        String fileName;
        String url;
        StringArray stems;      // ids in the model's output order
        int approxSizeMB;
    };

    const ModelInfo& getModelInfo (ModelType);
    String getStemDisplayName (const String& stemId);  // "vocals" -> "Vocals"
    String getModelsPageUrl();
    String getModelUrl (ModelType);   // download location (honours MYDAW_MODEL_BASE_URL)

    /** Where downloaded models are kept (an ASCII-only path on Windows, see .cpp). */
    File getModelsFolder();
    File getModelFile (ModelType);                      // may not exist yet
    bool isModelInstalled (ModelType);

    /** Checks the file header; fills `type` when it is a valid Demucs v4 model file. */
    bool isValidModelFile (const File&, ModelType* type = nullptr);

    /** Copies a user-chosen model file into the models folder. */
    Result installModelFile (const File& source, ModelType* typeOut = nullptr);

    constexpr double modelSampleRate = 44100.0;

    struct Stem
    {
        String id;                              // "vocals", "drums", ...
        std::shared_ptr<SampleData> audio;      // stereo, 44.1 kHz
    };

    /** Returns false from the callback to cancel. Called from worker threads. */
    using ProgressFn = std::function<bool (float progress, const String& status)>;

    /** Runs the separation synchronously (call it from a background thread).
        `numThreads` <= 0 picks a value from the number of CPU cores. */
    Result separate (const File& modelFile, const SampleData& input, Range<int> range, int numThreads,
                     std::vector<Stem>& result, ProgressFn progress);

    /** Checks the parallel split / cross-fade logic with a fake model (used by the tests). */
    bool selfTestPartMerging (String& details);

    /** The sum of all stems except vocals ("karaoke" / instrumental). */
    std::shared_ptr<SampleData> mixWithout (const std::vector<Stem>&, const String& excludedId);
}

//==============================================================================
/** Background job: downloads a model file. */
class ModelDownloadJob : public Thread
{
public:
    explicit ModelDownloadJob (Stems::ModelType);
    ~ModelDownloadJob() override;

    void run() override;

    double getProgress() const noexcept   { return progress.load(); }
    bool isFinished() const noexcept      { return finished.load(); }
    String getError() const;
    Stems::ModelType getType() const noexcept { return type; }

private:
    const Stems::ModelType type;
    std::atomic<double> progress { 0.0 };
    std::atomic<bool> finished { false };
    mutable CriticalSection lock;
    String error;
};

/** Background job: separates (part of) a sample into stems. */
class StemSeparationJob : public Thread
{
public:
    StemSeparationJob (const File& modelFile, std::shared_ptr<const SampleData> input, Range<int> range);
    ~StemSeparationJob() override;

    void run() override;
    void cancel()                          { cancelled = true; }

    float getProgress() const noexcept     { return progress.load(); }
    String getStatus() const;
    bool isFinished() const noexcept       { return finished.load(); }
    bool wasCancelled() const noexcept     { return cancelled.load(); }
    String getError() const;
    Range<int> getRange() const noexcept   { return range; }
    double getInputSampleRate() const      { return input->sampleRate; }

    /** Valid once isFinished() returns true. */
    std::vector<Stems::Stem>& getStems()   { return stems; }

private:
    const File modelFile;
    const std::shared_ptr<const SampleData> input;
    const Range<int> range;
    std::vector<Stems::Stem> stems;

    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false }, cancelled { false };
    mutable CriticalSection lock;
    String status, error;
};
