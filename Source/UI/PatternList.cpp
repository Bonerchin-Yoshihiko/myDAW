#include "UI/PatternList.h"

PatternList::PatternList (Session& s) : session (s)
{
    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (ListBox::backgroundColourId, Theme::panel);
    addAndMakeVisible (list);

    for (auto* b : { &newButton, &cloneButton, &deleteButton, &saveButton, &loadButton })
        addAndMakeVisible (b);

    newButton.setTooltip ("Create an empty pattern");
    cloneButton.setTooltip ("Duplicate the selected pattern");
    deleteButton.setTooltip ("Delete the selected pattern (also removes it from the song)");
    saveButton.setTooltip ("Save the selected pattern to a file (to reuse it in other projects)");
    loadButton.setTooltip ("Load a pattern file into this project");

    newButton.onClick = [this] { session.createPattern(); };
    cloneButton.onClick = [this] { session.duplicatePattern (session.getCurrentPatternId()); };
    deleteButton.onClick = [this]
    {
        const auto id = session.getCurrentPatternId();
        auto* p = session.getCurrentPattern();

        if (p == nullptr)
            return;

        const auto used = std::any_of (session.getProject().clips.begin(), session.getProject().clips.end(),
                                       [id] (const Clip& c) { return c.patternId == id; });

        if (! used)
        {
            session.deletePattern (id);
            return;
        }

        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                                   .withTitle ("Delete pattern")
                                                   .withMessage ("\"" + p->name + "\" is used in the song. Delete it and remove it from the song?")
                                                   .withButton ("Delete")
                                                   .withButton ("Cancel"),
                                [this, id] (int result) { if (result == 1) session.deletePattern (id); });
    };
    saveButton.onClick = [this] { savePattern(); };
    loadButton.onClick = [this] { loadPattern(); };

    session.addListener (this);
    refresh();
}

PatternList::~PatternList()
{
    session.removeListener (this);
}

void PatternList::paint (Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::dimText);
    g.setFont (Theme::font (12.0f, true));
    g.drawText ("PATTERNS", 10, 0, getWidth() - 20, 28, Justification::centredLeft);

    g.setFont (Theme::font (11.0f));
    g.drawFittedText ("Drag a pattern onto the song to place it.", 10, getHeight() - 26, getWidth() - 20, 22,
                      Justification::centredLeft, 2);

    g.setColour (Theme::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void PatternList::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1);
    r.removeFromTop (28);
    r.removeFromBottom (28);

    auto buttons2 = r.removeFromBottom (30).reduced (6, 3);
    auto buttons1 = r.removeFromBottom (30).reduced (6, 3);
    const auto w1 = buttons1.getWidth() / 3;
    newButton.setBounds (buttons1.removeFromLeft (w1).reduced (2, 0));
    cloneButton.setBounds (buttons1.removeFromLeft (w1).reduced (2, 0));
    deleteButton.setBounds (buttons1.reduced (2, 0));
    const auto w2 = buttons2.getWidth() / 2;
    saveButton.setBounds (buttons2.removeFromLeft (w2).reduced (2, 0));
    loadButton.setBounds (buttons2.reduced (2, 0));

    list.setBounds (r.reduced (0, 2));
}

int PatternList::getNumRows()
{
    return (int) session.getProject().patterns.size();
}

void PatternList::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
{
    auto& patterns = session.getProject().patterns;

    if (! isPositiveAndBelow (row, (int) patterns.size()))
        return;

    auto& p = *patterns[(size_t) row];
    auto r = Rectangle<int> (0, 0, width, height).reduced (6, 3);

    g.setColour (selected ? Theme::panelLighter : Theme::panelLight);
    g.fillRoundedRectangle (r.toFloat(), 4.0f);

    if (selected)
    {
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.2f);
    }

    g.setColour (p.colour);
    g.fillRoundedRectangle (r.removeFromLeft (6).toFloat(), 2.0f);
    r.removeFromLeft (6);

    g.setColour (Theme::text);
    g.setFont (Theme::font (14.0f, true));
    g.drawText (p.name, r.removeFromTop (r.getHeight() / 2 + 2), Justification::bottomLeft, true);

    g.setColour (Theme::dimText);
    g.setFont (Theme::font (11.5f));

    auto uses = (int) std::count_if (session.getProject().clips.begin(), session.getProject().clips.end(),
                                     [&p] (const Clip& c) { return c.patternId == p.id; });
    g.drawText (p.getLengthDescription() + "   " + String ((int) p.notes.size()) + " notes"
                    + (uses > 0 ? "   x" + String (uses) : String()),
                r, Justification::centredLeft, true);
}

void PatternList::selectedRowsChanged (int lastRowSelected)
{
    auto& patterns = session.getProject().patterns;

    if (isPositiveAndBelow (lastRowSelected, (int) patterns.size()))
        session.selectPattern (patterns[(size_t) lastRowSelected]->id);
}

void PatternList::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showRowMenu (row);
}

void PatternList::listBoxItemDoubleClicked (int row, const MouseEvent&)
{
    auto& patterns = session.getProject().patterns;

    if (isPositiveAndBelow (row, (int) patterns.size()))
        renamePattern (patterns[(size_t) row]->id);
}

var PatternList::getDragSourceDescription (const SparseSet<int>& rows)
{
    auto& patterns = session.getProject().patterns;

    if (rows.size() > 0 && isPositiveAndBelow (rows[0], (int) patterns.size()))
        return "pattern:" + String (patterns[(size_t) rows[0]]->id);

    return {};
}

String PatternList::getTooltipForRow (int)
{
    return "Click to edit, drag onto the song, double-click to rename, right-click for more";
}

void PatternList::sessionChanged (int changes)
{
    if ((changes & (Session::patternListChanged | Session::patternContentChanged | Session::selectionChanged
                    | Session::arrangementChanged | Session::projectReplaced)) != 0)
        refresh();
}

void PatternList::refresh()
{
    list.updateContent();

    auto& patterns = session.getProject().patterns;

    for (size_t i = 0; i < patterns.size(); ++i)
        if (patterns[i]->id == session.getCurrentPatternId() && ! list.isRowSelected ((int) i))
            list.selectRow ((int) i, false, true);

    list.repaint();
}

void PatternList::renamePattern (int patternId)
{
    auto* p = session.getProject().findPattern (patternId);

    if (p == nullptr)
        return;

    auto* w = new AlertWindow ("Rename pattern", {}, MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", p->name);
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    w->enterModalState (true, ModalCallbackFunction::create ([this, patternId, w] (int result)
    {
        if (result == 1)
        {
            if (auto* pat = session.getProject().findPattern (patternId))
            {
                const auto name = w->getTextEditorContents ("name").trim();

                if (name.isNotEmpty())
                {
                    pat->name = name;
                    session.patternChanged (patternId);
                    session.notify (Session::patternListChanged);
                }
            }
        }
    }), true);
}

void PatternList::showRowMenu (int row)
{
    auto& patterns = session.getProject().patterns;

    if (! isPositiveAndBelow (row, (int) patterns.size()))
        return;

    const auto id = patterns[(size_t) row]->id;
    PopupMenu m;
    m.addItem ("Rename...", [this, id] { renamePattern (id); });
    m.addItem ("Duplicate", [this, id] { session.duplicatePattern (id); });
    m.addItem ("Save to file...", [this, id] { session.selectPattern (id); savePattern(); });
    m.addSeparator();
    m.addItem ("Delete", [this, id] { session.selectPattern (id); deleteButton.triggerClick(); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

void PatternList::savePattern()
{
    auto* p = session.getCurrentPattern();

    if (p == nullptr)
        return;

    auto& settings = session.getPluginManager().getSettings();
    File dir (settings.getValue ("patternDir", File::getSpecialLocation (File::userDocumentsDirectory).getFullPathName()));
    const auto patternId = p->id;

    chooser = std::make_unique<FileChooser> ("Save pattern", dir.getChildFile (File::createLegalFileName (p->name) + Session::patternExtension),
                                             String ("*") + Session::patternExtension);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this, patternId] (const FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == File())
            return;

        file = file.withFileExtension (Session::patternExtension);
        session.getPluginManager().getSettings().setValue ("patternDir", file.getParentDirectory().getFullPathName());

        const auto result = session.exportPattern (patternId, file);

        if (result.failed() && onError)
            onError (result.getErrorMessage());
    });
}

void PatternList::loadPattern()
{
    auto& settings = session.getPluginManager().getSettings();
    File dir (settings.getValue ("patternDir", File::getSpecialLocation (File::userDocumentsDirectory).getFullPathName()));

    chooser = std::make_unique<FileChooser> ("Load pattern", dir, String ("*") + Session::patternExtension);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::canSelectMultipleItems,
                          [this] (const FileChooser& fc)
    {
        StringArray problems;

        for (auto& file : fc.getResults())
        {
            session.getPluginManager().getSettings().setValue ("patternDir", file.getParentDirectory().getFullPathName());
            const auto result = session.importPattern (file, problems);

            if (result.failed())
                problems.add (result.getErrorMessage());
        }

        if (! problems.isEmpty() && onError)
            onError (problems.joinIntoString ("\n"));
    });
}
