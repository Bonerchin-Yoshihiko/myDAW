#include "UI/AppFiles.h"

namespace
{
   #if JUCE_IOS
    /** iOS reports files inside the app both as /private/var/... and /var/... */
    String withoutPrivatePrefix (const String& path)
    {
        return path.startsWith ("/private/var/") ? path.substring (8) : path;
    }

    bool isInsideApp (const File& f)
    {
        const auto home = withoutPrivatePrefix (File::getSpecialLocation (File::userHomeDirectory).getFullPathName());
        return withoutPrivatePrefix (f.getFullPathName()).startsWith (home + "/");
    }

    /** Copies a picked file into `folder` (re-using an identical earlier copy) and returns the copy. */
    File copyIntoApp (URL url, const File& folder, String& error)
    {
        const auto name = File::createLegalFileName (URL::removeEscapeChars (url.getFileName()));

        if (! folder.createDirectory())
        {
            error = "Could not create " + folder.getFullPathName();
            return {};
        }

        auto in = url.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress));

        if (in == nullptr)
        {
            error = "Could not read " + name;
            return {};
        }

        const auto existing = folder.getChildFile (name);
        const auto length = in->getTotalLength();

        if (existing.existsAsFile() && length >= 0 && existing.getSize() == length)
            return existing;   // imported before

        const auto target = existing.existsAsFile() ? folder.getNonexistentChildFile (existing.getFileNameWithoutExtension(),
                                                                                       existing.getFileExtension(), false)
                                                    : existing;
        {
            FileOutputStream out (target);

            if (out.failedToOpen() || out.writeFromInputStream (*in, -1) < 0 || ! out.getStatus().wasOk())
            {
                error = "Could not copy " + name + " into the app";
                target.deleteFile();
                return {};
            }
        }

        return target;
    }

    void placeAboveKeyboard (Component& c)
    {
        if (auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto safe = display->safeAreaInsets.subtractedFrom (display->totalArea);
            c.setTopLeftPosition (safe.getCentreX() - c.getWidth() / 2, safe.getY() + 8);
        }
    }
   #endif
}

File AppFiles::getDocumentsFolder()
{
    return File::getSpecialLocation (File::userDocumentsDirectory);
}

File AppFiles::getDocumentsSubfolder (const String& name)
{
    auto folder = name.isEmpty() ? getDocumentsFolder() : getDocumentsFolder().getChildFile (name);
    folder.createDirectory();
    return folder;
}

String AppFiles::describeLocation (const File& f)
{
   #if JUCE_IOS
    const auto docs = getDocumentsFolder();
    const auto relative = withoutPrivatePrefix (f.getFullPathName()).fromFirstOccurrenceOf (withoutPrivatePrefix (docs.getFullPathName()) + "/",
                                                                                            false, false);
    if (relative.isNotEmpty())
        return "Files app > On My " + String (SystemStats::getDeviceDescription().containsIgnoreCase ("iPad") ? "iPad" : "iPhone")
               + " > myDAW > " + relative.replace ("/", " > ");
   #endif

    return f.getFullPathName();
}

//==============================================================================
void AppFiles::chooseFilesToOpen (std::unique_ptr<FileChooser>& holder, const OpenRequest& request,
                                  std::function<void (const Array<File>&)> done)
{
    auto flags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;

    if (request.multipleFiles)
        flags |= FileBrowserComponent::canSelectMultipleItems;

   #if JUCE_IOS
    const auto wildcard = request.appFileType ? String() : request.wildcard;
    const auto copyInto = request.copyInto != File() ? request.copyInto : getDocumentsFolder().getChildFile ("Imported");

    holder = std::make_unique<FileChooser> (request.title, getDocumentsFolder(), wildcard);
    holder->launchAsync (flags, [done, copyInto] (const FileChooser& fc)
    {
        Array<File> files;
        StringArray errors;

        for (auto& url : fc.getURLResults())
        {
            const auto local = url.isLocalFile() ? url.getLocalFile() : File();

            if (local != File() && isInsideApp (local) && local.existsAsFile())
            {
                files.add (File (withoutPrivatePrefix (local.getFullPathName())));
                continue;
            }

            String error;
            const auto copy = copyIntoApp (url, copyInto, error);

            if (copy != File())
                files.add (copy);
            else
                errors.add (error);
        }

        if (! errors.isEmpty())
            AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                                       .withTitle ("Open")
                                                       .withMessage (errors.joinIntoString ("\n"))
                                                       .withButton ("OK"),
                                    nullptr);

        done (files);
    });
   #else
    holder = std::make_unique<FileChooser> (request.title, request.initialFolder, request.wildcard);
    holder->launchAsync (flags, [done] (const FileChooser& fc) { done (fc.getResults()); });
   #endif
}

//==============================================================================
void AppFiles::chooseFileToSave (std::unique_ptr<FileChooser>& holder, const SaveRequest& request,
                                 std::function<void (const File&)> done)
{
   #if JUCE_IOS
    ignoreUnused (holder);
    const auto folder = getDocumentsSubfolder (request.mobileSubfolder);
    const auto extension = request.extension;

    askForText (request.title,
                "Saved on this " + String (SystemStats::getDeviceDescription().containsIgnoreCase ("iPad") ? "iPad" : "iPhone")
                    + " in the myDAW folder (Files app > On My "
                    + (SystemStats::getDeviceDescription().containsIgnoreCase ("iPad") ? "iPad" : "iPhone") + " > myDAW).",
                request.suggestedFile.getFileNameWithoutExtension(), "Save", request.parent,
                [folder, extension, done] (const String& name)
    {
        const auto legal = File::createLegalFileName (name).trim();

        if (legal.isEmpty())
            return;

        const auto file = folder.getChildFile (legal).withFileExtension (extension);

        if (! file.existsAsFile())
        {
            done (file);
            return;
        }

        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                                   .withTitle ("Replace?")
                                                   .withMessage ("\"" + file.getFileName() + "\" already exists. Replace it?")
                                                   .withButton ("Replace")
                                                   .withButton ("Cancel"),
                                [file, done] (int result) { if (result == 1) done (file); });
    });
   #else
    const auto extension = request.extension;
    holder = std::make_unique<FileChooser> (request.title, request.suggestedFile, "*" + extension);
    holder->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                             | FileBrowserComponent::warnAboutOverwriting,
                         [done, extension] (const FileChooser& fc)
    {
        const auto file = fc.getResult();
        done (file == File() ? File() : file.withFileExtension (extension));
    });
   #endif
}

void AppFiles::offerToShare (const File& file, Component* parent)
{
   #if JUCE_IOS && JUCE_CONTENT_SHARING
    static ScopedMessageBox shareSheet;
    shareSheet = ContentSharer::shareFilesScoped ({ URL (file) }, [] (bool, const String&) {}, parent);
   #else
    ignoreUnused (file, parent);
   #endif
}

//==============================================================================
void AppFiles::askForText (const String& title, const String& message, const String& initialText,
                           const String& okButton, Component* parent, std::function<void (const String&)> done)
{
    auto* w = new AlertWindow (title, message, MessageBoxIconType::NoIcon, parent);
    w->addTextEditor ("text", initialText);
    w->addButton (okButton, 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([w, done] (int result)
    {
        const auto text = w->getTextEditorContents ("text").trim();

        if (result == 1 && text.isNotEmpty() && done)
            done (text);
    }), true);

   #if JUCE_IOS
    placeAboveKeyboard (*w);

    if (auto* editor = w->getTextEditor ("text"))
    {
        editor->grabKeyboardFocus();
        editor->selectAll();
    }
   #endif
}
