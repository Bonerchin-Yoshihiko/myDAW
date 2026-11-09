#pragma once

#include "Common.h"

/** Opening and saving files the same way on every platform.

    Desktop: the native file dialogs.

    iPhone / iPad: apps can only keep files in their own container. The app's "Documents"
    folder is shown in the Files app as "On My iPhone > myDAW", so that is where projects,
    patterns and exports are saved (the app asks for a name instead of showing a save dialog).
    Files picked from other places (iCloud Drive, other apps ...) can only be read for a moment,
    so they are copied into Documents first; projects then keep working. */
namespace AppFiles
{
    /** Where projects are saved by default (the app's Documents folder on iOS). */
    File getDocumentsFolder();

    /** A sub-folder of the Documents folder (created if needed). */
    File getDocumentsSubfolder (const String& name);

    struct OpenRequest
    {
        String title;
        File initialFolder;
        String wildcard;                     // e.g. "*.wav;*.mp3"
        bool multipleFiles = false;
        bool appFileType = false;            // our own extension: iOS cannot filter by it, so it lists every file
        File copyInto;                       // iOS: folder for copies of files from outside the app
    };

    /** Lets the user pick files; `done` gets readable local files (empty if cancelled).
        `holder` keeps the dialog alive and must outlive it (usually a member). */
    void chooseFilesToOpen (std::unique_ptr<FileChooser>& holder, const OpenRequest&,
                            std::function<void (const Array<File>&)> done);

    struct SaveRequest
    {
        String title;
        File suggestedFile;                  // desktop: initial file; iOS: its name is suggested
        String extension;                    // e.g. ".wav"
        String mobileSubfolder;              // iOS: sub-folder of Documents ("" = Documents itself)
        Component* parent = nullptr;         // what a name prompt is centred around
    };

    /** Lets the user choose where to save; `done` gets the file with the extension
        (or File() if cancelled). Overwriting an existing file is confirmed first. */
    void chooseFileToSave (std::unique_ptr<FileChooser>& holder, const SaveRequest&,
                           std::function<void (const File&)> done);

    /** After a file was exported: on iOS this offers the share sheet (AirDrop, save to Files,
        send to another app). Does nothing on desktops, where the user picked the folder. */
    void offerToShare (const File&, Component* parent);

    /** A short note saying where a saved file can be found, for messages. */
    String describeLocation (const File&);

    /** Asks for a line of text (a name). `done` is only called when the user pressed OK
        with something non-empty. On phones the box sits above the on-screen keyboard. */
    void askForText (const String& title, const String& message, const String& initialText,
                     const String& okButton, Component* parent, std::function<void (const String&)> done);
}
