// scalpel-editor production popup stubs (phase 6 step 7).
//
// ListBox and Menu stay compiled so autocomplete, call tips, and context menus
// link, but this Wayland shell does not create real popup windows yet. Requests
// are logged to stderr and never report a successful visible popup. editorTest
// links its own inspectable TestListBox instead of this translation unit.
// Real popup windows are follow-on work after this roadmap.

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Platform.h"

namespace Scintilla::Internal {

namespace {

void LogPopup(const char *message) noexcept {
	std::fprintf(stderr, "scalpel-editor popup stub: %s\n", message);
}

// In-memory list storage so core list APIs do not crash, but Create never
// assigns a WindowID so the list is never a real window.
class StubListBox final : public ListBox {
public:
	void SetFont(const Font *) override {}

	void Create(Window &, int, Point, int) override {
		LogPopup("ListBox::Create (no window; autocomplete popup not implemented)");
		// Leave wid null: Window::Created() stays false.
		items.clear();
		selection = -1;
	}

	void SetAverageCharWidth(int) override {}
	void SetVisibleRows(int rows) override { visibleRows = rows > 0 ? rows : 1; }
	int GetVisibleRows() const override { return visibleRows; }

	PRectangle GetDesiredRect() override {
		return PRectangle();
	}

	int CaretFromEdge() override { return 0; }

	void Clear() noexcept override {
		items.clear();
		selection = -1;
	}

	void Append(char *s, int type = -1) override {
		items.push_back({s ? s : "", type});
	}

	int Length() override {
		return static_cast<int>(items.size());
	}

	void Select(int n) override {
		selection = n;
	}

	int GetSelection() override {
		return selection;
	}

	int Find(const char *prefix) override {
		if (!prefix) {
			return -1;
		}
		const std::string_view match = prefix;
		for (size_t i = 0; i < items.size(); i++) {
			if (items[i].text.compare(0, match.size(), match) == 0) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	std::string GetValue(int n) override {
		if (n < 0 || static_cast<size_t>(n) >= items.size()) {
			return {};
		}
		return items[static_cast<size_t>(n)].text;
	}

	void RegisterImage(int, const char *) override {
		LogPopup("ListBox::RegisterImage (ignored)");
	}

	void RegisterRGBAImage(int, int, int, const unsigned char *) override {
		LogPopup("ListBox::RegisterRGBAImage (ignored)");
	}

	void ClearRegisteredImages() override {}

	void SetDelegate(IListBoxDelegate *) override {}

	void SetList(const char *list, char separator, char typesep) override {
		Clear();
		if (!list) {
			return;
		}
		std::string word;
		int type = -1;
		for (const char *p = list; ; p++) {
			if (*p == separator || *p == '\0') {
				if (!word.empty()) {
					items.push_back({word, type});
					word.clear();
					type = -1;
				}
				if (*p == '\0') {
					break;
				}
			} else if (*p == typesep) {
				type = 0;
				// type digits follow typesep in the usual list format; keep simple.
			} else {
				word.push_back(*p);
			}
		}
	}

	void SetOptions(ListOptions) override {}

private:
	struct Item {
		std::string text;
		int type = -1;
	};
	std::vector<Item> items;
	int selection = -1;
	int visibleRows = 5;
};

}

ListBox::ListBox() noexcept = default;
ListBox::~ListBox() noexcept = default;

std::unique_ptr<ListBox> ListBox::Allocate() {
	LogPopup("ListBox::Allocate");
	return std::make_unique<StubListBox>();
}

Menu::Menu() noexcept : mid{} {
}

void Menu::CreatePopUp() {
	LogPopup("Menu::CreatePopUp (context menu not implemented)");
	mid = nullptr;
}

void Menu::Destroy() noexcept {
	mid = nullptr;
}

void Menu::Show(Point pt, const Window &) {
	std::fprintf(stderr, "scalpel-editor popup stub: Menu::Show at %.1f,%.1f (not implemented)\n",
		pt.x, pt.y);
}

}
