#ifndef USE_GI_MODULE
// no inline as the implementation is in another TU
// #define GI_INLINE 1
#include <gtk/gtk.hpp>
#else
// optional, but recommended if gi macros are used
#include <gi/gi_inc.hpp>
// we use some gtk macros
#include <gtk/gtk.h>
// import recursive module
import gi.repo.gtk.rec;
#endif

// adapt to API as needed
#if GTK_CHECK_VERSION(4, 0, 0)
#define GTK4 1
#endif

#include <fstream>
#include <iostream>
#include <tuple>
#include <vector>

namespace GLib = gi::repository::GLib;
namespace GObject_ = gi::repository::GObject;
namespace Gtk = gi::repository::Gtk;

static GLib::MainLoop loop;

#ifdef GI_CLASS_IMPL
// based on python-gtk3 example
// https://python-gtk-3-tutorial.readthedocs.io/en/latest/treeview.html

// list of tuples for each software,
// containing the software name, initial release, and main programming languages
const std::vector<std::tuple<std::string, int, std::string>> software_list{
    std::make_tuple("Firefox", 2002, "C++"),
    std::make_tuple("Eclipse", 2004, "Java"),
    std::make_tuple("Pitivi", 2004, "Python"),
    std::make_tuple("Netbeans", 1996, "Java"),
    std::make_tuple("Chrome", 2008, "C++"),
    std::make_tuple("Filezilla", 2001, "C++"),
    std::make_tuple("Bazaar", 2005, "Python"),
    std::make_tuple("Git", 2005, "C"),
    std::make_tuple("Linux Kernel", 1991, "C"),
    std::make_tuple("GCC", 1987, "C"),
    std::make_tuple("Frostwire", 2004, "Java")};

class TreeViewFilterWindow : public Gtk::impl::WindowImpl
{
  typedef TreeViewFilterWindow self_type;

  Gtk::ListStore store_;
  Gtk::TreeModelFilter language_filter_;
  std::string current_filter_language_;

public:
  TreeViewFilterWindow() : Gtk::impl::WindowImpl(this)
  {
    Gtk::Window &self = *(this);
    self.set_title("TreeView filter demo");
#ifdef GTK4
#else
    self.set_border_width(10);
#endif

    // set up the grid in which elements are positioned
    auto grid = Gtk::Grid::new_();
    grid.set_column_homogeneous(true);
    grid.set_row_homogeneous(true);
#ifdef GTK4
    self.set_child(grid);
#if 0
    // this could work, but the annotation for .load_from_data
    // is too unstable across versions
    { // migrate call above to CSS style
      const char *css = "grid { margin: 10px; }";
      auto provider = Gtk::CssProvider::new_();
      provider.load_from_data((guint8 *)css, -1);
      grid.get_style_context().add_provider(
          provider, Gtk::STYLE_PROVIDER_PRIORITY_USER_);
    }
#endif
#else
    self.add(grid);
#endif

    // create ListStore model
    store_ = Gtk::ListStore::new_type_<std::string, int, std::string>();
    for (auto &e : software_list) {
      auto it = store_.append();
      GObject_::Value cols[] = {std::get<0>(e), std::get<1>(e), std::get<2>(e)};
      for (unsigned i = 0; i < G_N_ELEMENTS(cols); ++i) {
        store_.set_value(it, i, cols[i]);
      }
    }

    // create the filter, feeding it with the liststore model
    auto treemodel = store_.interface_(gi::interface_tag<Gtk::TreeModel>());
    language_filter_ =
        gi::object_cast<Gtk::TreeModelFilter>(treemodel.filter_new(nullptr));
    // set the filter function
    language_filter_.set_visible_func(
        gi::mem_fun(&self_type::language_filter_func, this));

    // create the treeview, make it use the filter as a model, and add
    // columns
    auto treeview = Gtk::TreeView::new_with_model(language_filter_);
    int i = 0;
    for (auto &e : {"Software", "Release Year", "Programming Language"}) {
      auto renderer = Gtk::CellRendererText::new_();
      auto column = Gtk::TreeViewColumn::new_(e, renderer, {{"text", i}});
      treeview.append_column(column);
      ++i;
    }

    // create buttons to filter by programming language, and set up their
    // events
    std::vector<Gtk::Widget> buttons;
    for (auto &prog_language : {"Java", "C", "C++", "Python", "None"}) {
      auto button = Gtk::Button::new_with_label(prog_language);
      buttons.push_back(button);
      button.signal_clicked().connect(
          gi::mem_fun(&self_type::on_selection_button_clicked, this));
    }

    // set up the layout;
    // put the treeview in a scrollwindow, and the buttons in a row
    auto scrollable_treelist = Gtk::ScrolledWindow::new_();
    scrollable_treelist.set_vexpand(true);
    grid.attach(scrollable_treelist, 0, 0, 8, 10);
    grid.attach_next_to(
        buttons[0], scrollable_treelist, Gtk::PositionType::BOTTOM_, 1, 1);
    auto it = buttons.begin() + 1;
    while (it != buttons.end()) {
      grid.attach_next_to(*it, *(it - 1), Gtk::PositionType::RIGHT_, 1, 1);
      ++it;
    }
#ifdef GTK4
    scrollable_treelist.set_child(treeview);
    self.show();
#else
    scrollable_treelist.add(treeview);
    self.show_all();
#endif
  }

  bool language_filter_func(Gtk::TreeModel filter, Gtk::TreeIter_Ref it) const
  {
    if (current_filter_language_.empty() || current_filter_language_ == "None")
      return true;
    return current_filter_language_ ==
           filter.get_value(it, 2).get_value<std::string>();
  }

  void on_selection_button_clicked(Gtk::Button button)
  {
    // set the current language filter to the button's label
    current_filter_language_ = button.get_label();
    std::cout << current_filter_language_ << " language selected!" << std::endl;
    //  update the filter, which updates in turn the view
    language_filter_.refilter();
  }
};

// other part based on gtkmm builder example
namespace Gio = gi::repository::Gio;

auto templ = R"|(
<interface>
  <template class="FooWidget" parent="GtkBox">
    <property name="orientation">GTK_ORIENTATION_HORIZONTAL</property>
    <property name="spacing">4</property>
    <child>
      <object class="GtkButton" id="hello_button">
        <property name="label">Hello World</property>
        <signal name="clicked" handler="hello_button_clicked"/>
        <signal name="clicked" handler="hello_button_clicked_object" object="label" swapped="no"/>
        <signal name="clicked" handler="hello_button_clicked_object_swapped" object="label" swapped="yes"/>
      </object>
    </child>
    <child>
      <object class="GtkButton" id="goodbye_button">
        <property name="label">Goodbye World</property>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="label">
        <property name="label">Greetings World</property>
      </object>
    </child>
  </template>
</interface>
)|";

auto templ_child = R"|(
        <child>
          <object class="FooWidget" id="foowidget1"/>
        </child>
)|";

using WidgetTemplateHelper = Gtk::impl::WidgetTemplateHelper;

class FooWidget : public Gtk::impl::BoxImpl, public WidgetTemplateHelper
{
  using self_type = FooWidget;

public:
  static void custom_class_init(GtkBoxClass *klass, gpointer)
  {
    // this is similar to the C case
    // plain C functions are used, as klass struct has no direct equivalent
    auto wklass = GTK_WIDGET_CLASS(klass);
    auto bytes = GLib::Bytes::new_static((const guint8 *)templ, strlen(templ));
    gtk_widget_class_set_template(wklass, bytes.gobj_());
    gtk_widget_class_bind_template_child_full(wklass, "hello_button", TRUE, 0);
    // chain up
    WidgetTemplateHelper::custom_class_init<gi::register_type<self_type>>(
        klass, nullptr);
  }

public:
  Gtk::Button hello_;

  void on_hello(Gtk::Button) { std::cout << "Hi" << std::endl; }

  void on_hello_tail(Gtk::Button b, Gtk::Label l)
  {
    g_assert(l.gobj_type_() == GTK_TYPE_LABEL);
    g_assert(b.gobj_type_() == GTK_TYPE_BUTTON);
    std::cout << "Hi tail" << std::endl;
  }

  void on_hello_head(Gtk::Label l, Gtk::Button b)
  {
    g_assert(l.gobj_type_() == GTK_TYPE_LABEL);
    g_assert(b.gobj_type_() == GTK_TYPE_BUTTON);
    std::cout << "Hi head" << std::endl;
  }

  FooWidget(const InitData &id)
      : Gtk::impl::BoxImpl(this, id, "FooWidget"),
        WidgetTemplateHelper(object_())
  {
    // skip registration case
    if (!id) {
      // as we have defined a get_type_() below, that should not happen
      g_assert_not_reached();
      return;
    }

    // locate object
    hello_ = gi::object_cast<Gtk::Button>(
        get_template_child(gobj_type_(), "hello_button"));

    // setup signal
    // NOTE the "manual" signature here should suitably match
    // (also considering object/swapped flags, etc)
    auto ok = set_handler<void(Gtk::Button)>(
        "hello_button_clicked", gi::mem_fun(&self_type::on_hello, this));
    g_assert(ok);
    ok = set_handler<void(Gtk::Button, Gtk::Label), ConnectObject::TAIL>(
        "hello_button_clicked_object",
        gi::mem_fun(&self_type::on_hello_tail, this));
    g_assert(ok);
    ok = set_handler<void(Gtk::Label, Gtk::Button), ConnectObject::HEAD>(
        "hello_button_clicked_object_swapped",
        gi::mem_fun(&self_type::on_hello_head, this));
    g_assert(ok);
  }

  static GType get_type_()
  {
    return register_type_<FooWidget>("FooWidget", 0, {}, {}, {});
  }
};

class ExampleWindow : public Gtk::impl::WindowImpl
{
  using self_type = ExampleWindow;

public:
  ExampleWindow(Gtk::Window base, Gtk::Builder builder)
      : Gtk::impl::WindowImpl(base, this, "ExampleWindow")
  // custom name parameter is optional, but can be provided if used in .ui
  {
    (void)builder;
    setup();
  }

  // name parameter is required and specifies registered type as-is
  // (so some proper namespace prefix is advisable in real case)
  ExampleWindow(const InitData &id)
      : Gtk::impl::WindowImpl(this, id, "ExampleWindow")
  {
    // skip registration case
    if (id)
      setup();
  }

  void setup()
  {
    auto actions = Gio::SimpleActionGroup::new_();
    auto am = Gio::ActionMap(actions);
    auto action = Gio::SimpleAction::new_("help");
    action.signal_activate().connect(gi::mem_fun(&self_type::on_help, this));
    am.add_action(action);
    insert_action_group("win", actions);
  }

  void on_help(Gio::Action, GLib::Variant) { std::cout << "Help" << std::endl; }

  // subclass_type == 0; UI specifies GtkWindow
  // subclass_type < 0; UI specifies GIOBJECT__ExampleWindow
  //    in either cases above; manual C++ association is needed
  //        (uses first constructor)
  //        -> NOT recommended
  // subclass_type > 0; UI specifies ExampleWindow;
  //    all is properly created by (C-side) instance construction
  //        (uses second constructor)
  //        -> recommended
  // subclass_type > 1; also insert a FooWidget
  //    (also all created by C-side construction)
  static Gtk::Window build(int subclass_type)
  {
    const char *UIFILE = G_STRINGIFY(EXAMPLES_DIR) "/gtk-builder.ui";
    const char *WINID = "window1";

    auto builder = Gtk::Builder::new_();
    // if the subtype has additional functionality (e.g. method overrides)
    // then builder should instantiate using that type,
    // otherwise base type suffices
    // NOTE in the former case, there is a "short time" that the GObject side
    // exists without a corresponding C++ side, so any attempt to use the
    // extended parts (e.g. method calls) will be unfortunate
    if (subclass_type) {
      std::string WINCLASS = "GtkWindow";
      std::ifstream f(UIFILE, std::ios::binary);
      std::string ui;
      std::getline(f, ui, '\0');

      auto index = ui.find(WINCLASS);
      assert(index != ui.npos);
      ui.replace(ui.begin() + index, ui.begin() + index + WINCLASS.size(),
          subclass_type < 0 ? "GIOBJECT__ExampleWindow" : "ExampleWindow");
      if (subclass_type > 1) {
        // sprinkle a template instance
        std::string MARKER = "<!-- INSERT -->";
        index = ui.find(MARKER);
        ui.replace(ui.begin() + index, ui.begin() + index + MARKER.size(),
            templ_child);
        // ensure type registered
        gi::register_type<FooWidget>();
      }
      // now we got a UI file that references the subclass type
      // ensure the latter is registered
      if (subclass_type < 0) {
        // use a temporary instance
        gi::make_ref<ExampleWindow, gi::construct_cpp_t>(nullptr, builder);
      } else {
        // a new cleaner way
        gi::register_type<ExampleWindow>();
      }
      builder.add_from_string(ui, -1);
    } else {
      builder.add_from_file(UIFILE);
    }
    if (false) {
      // some compile checks
      builder.get_object(WINID);
      builder.get_object<Gtk::Window>(WINID);
    }
    if (subclass_type > 1) {
      // verify proper C++ side setup
      auto foo_obj =
          builder.get_object<FooWidget::baseclass_type>("foowidget1");
      assert(foo_obj);
      auto foo = gi::ref_ptr_cast<FooWidget>(foo_obj);
      assert(foo);
      assert(foo->hello_);
    }
    // the special _derived may be needed to setup C++ side and associate
    return subclass_type > 0 ? builder.get_object<Gtk::Window>(WINID)
                             : builder.get_object_derived<self_type>(WINID);
  }
};
#endif // GI_CLASS_IMPL

int
main(int argc, char **argv)
{
#ifdef GTK4
  (void)argc;
  (void)argv;
  gtk_init();
#else
  gtk_init(&argc, &argv);
#endif

  loop = GLib::MainLoop::new_();

  // recommended general approach iso stack based
  // too much vmethod calling which is not safe for plain case
  Gtk::Window win;
#ifdef GI_CLASS_IMPL
  // silly compile/link check
  static_assert(gi::transfer_full.value == 1, "");
  if (argc == gi::transfer_full.value) {
    win = gi::make_ref<TreeViewFilterWindow>();
  } else {
    win = ExampleWindow::build(std::stoi(argv[1]));
  }
  // TODO auto-handle arg ignore ??
#ifdef GTK4
  win.signal_close_request().connect([](Gtk::Window) {
    loop.quit();
    return true;
  });
  win.show();
#else
  win.signal_destroy().connect([](Gtk::Widget) { loop.quit(); });
  win.show_all();
#endif
#else // GI_CLASS_IMPL
  (void)win;
#endif

  loop.run();
}
