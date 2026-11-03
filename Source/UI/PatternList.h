#pragma once

#include "Session.h"
#include "UI/Theme.h"

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
    TextButton newButton { "New" }, cloneButton { "Clone" }, deleteButton { "Delete" },
               saveButton { "Save..." }, loadButton { "Load..." };
    std::unique_ptr<FileChooser> chooser;
};
