#pragma once

#include "Session.h"
#include "UI/Theme.h"
#include "UI/Touch.h"

/** The pattern library on the left: create / duplicate / delete / save / load patterns,
    and drag them onto the song timeline. */
class PatternList : public Component,
                    private ListBoxModel,
                    private Session::Listener
{
public:
    explicit PatternList (Session&);
    ~PatternList() override;

    void paint (Graphics&) override;
    void resized() override;
    void setSmallScreen (bool);

    // mouse events of the list rows (for touch: long press = menu)
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;

    std::function<void (const String&)> onError;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const MouseEvent&) override;
    void listBoxItemClicked (int row, const MouseEvent&) override;
    var getDragSourceDescription (const SparseSet<int>& rows) override;
    String getTooltipForRow (int row) override;

    void sessionChanged (int changes) override;
    void refresh();
    void renamePattern (int patternId);
    void showRowMenu (int row);
    void savePattern();
    void loadPattern();

    Session& session;
    ListBox list;
    bool smallScreen = false;
    LongPress longPress;
    // with a finger, moving sideways drags a pattern to the song; up and down scrolls the list
    bool isTouchGesture = false, touchDragsPattern = false;
    TextButton newButton { "New" }, cloneButton { "Clone" }, deleteButton { "Delete" },
               saveButton { "Save..." }, loadButton { "Load..." };
    std::unique_ptr<FileChooser> chooser;
};
