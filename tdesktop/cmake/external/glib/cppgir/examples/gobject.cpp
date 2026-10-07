// no code generation is needed
// all required functionality is required by the basic code
#include <gi/gi.hpp>

#include <iostream>

namespace GObject_ = gi::repository::GObject;

static const int DEFAULT_AGE = 10;
static const char *PERSON_TYPE = "GIPerson";

// class must be a ObjectImpl to support properties and signals
class Person : public GObject_::impl::ObjectImpl
{
  using self_type = Person;

private:
  // the implementation/definition part of the properties
  // the setup parameters shadow the corresponding g_param_spec_xxx
  // so in practice define the property (name, nick, description)
  // along with min, max, default and so (where applicable)
  // (could also be in a constructor initializer list,
  // but this way it applies to any constructor)
  // this provides interface to set/get the actual value
  gi::property<int> prop_age_{this, "age", "age", "age", 0, 100, DEFAULT_AGE};
  gi::property<std::string> prop_firstname_{
      this, "firstname", "firstname", "firstname", ""};
  gi::property<std::string> prop_lastname_{
      this, "lastname", "lastname", "lastname", ""};

public:
  // likewise for signal
  // public because there is no extra interface for owning class
  // (both owner and outside can connect and/or emit)
  // btw, using Person in this signature would not be the way to go,
  // should stick to a plain wrapped type
  gi::signal<void(GObject_::Object, int)> signal_trigger{this, "trigger"};
  // std::string also works here,
  // but that would cost an extra allocation during invocation
  gi::signal<void(GObject_::Object, gi::cstring_v)> signal_example{
      this, "example"};

public:
  // old-style C++ first
  Person() : ObjectImpl(this) {}

  // new-style C-first;
  // must use proper signature for constructor and superclass
  // this also registers a "public" GType, whose name must be specified
  Person(const InitData &id) : ObjectImpl(this, id, PERSON_TYPE) {}

  // usually the above constructor is also used to during GType registration,
  // as such "dummy instantiation" auto-magically collects property info, etc)
  // however, it does involve an "incomplete" instantiation (with no .gobj_())
  // if such is considered too inelegant or too costly, then alternatively
  // a ::get_type_() can be defined that is then used to register type instead
  // however, as the example below demonstrates, this may be much less ergonomic
  // so it is likely only useful if really so desired or in advanced cases)
  // (e.g. subclass-ing a subclass)
  static GType get_type_disabled() // not really used due to extra suffix
  {
    // if this is used, the member initializers only need to mention name
    // no longer the full spec(ification)
    // (as the name then only serves to link the propertyspec to actual storage)
    return register_type_<Person>(PERSON_TYPE, 0, {},
        {{&self_type::prop_age_, "age", "age", "age", 0, 100, DEFAULT_AGE},
            {&self_type::prop_firstname_, "firstname", "firstname", "firstname",
                ""},
            {&self_type::prop_lastname_, "lastname", "lastname", "lastname",
                ""}},
        {{&self_type::signal_trigger, "trigger"},
            {&self_type::signal_example, "example"}});
  }

  // the public counterpart providing the same interface
  // as with any wrapped object's predefined properties
  gi::property_proxy<int> prop_age() { return prop_age_.get_proxy(); }

  gi::property_proxy<std::string> prop_firstname()
  {
    return prop_firstname_.get_proxy();
  }

  gi::property_proxy<std::string> prop_lastname()
  {
    return prop_lastname_.get_proxy();
  }

  void action(int id)
  {
    std::cout << "Changing the properties of 'p'" << std::endl;
    prop_firstname_ = "John";
    prop_lastname_ = "Doe";
    prop_age_ = 43;
    std::cout << "Done changing the properties of 'p'" << std::endl;
    // we were triggered after all
    signal_trigger.emit(id);
  }
};

void
on_firstname_changed(GObject_::Object, GObject_::ParamSpec)
{
  std::cout << "- firstname changed!" << std::endl;
}

void
on_lastname_changed(GObject_::Object, GObject_::ParamSpec)
{
  std::cout << "- lastname changed!" << std::endl;
}

void
on_age_changed(GObject_::Object, GObject_::ParamSpec)
{
  std::cout << "- age changed!" << std::endl;
}

int
main(int /*argc*/, char ** /*argv*/)
{
  Person p;
  // should have default age
  assert(p.prop_age().get() == DEFAULT_AGE);
  // register some handlers that will be called when the values of the
  // specified parameters are changed
  p.prop_firstname().signal_notify().connect(&on_firstname_changed);
  p.prop_lastname().signal_notify().connect(on_lastname_changed);
  p.prop_age().signal_notify().connect(&on_age_changed);

  // now change the properties and see that the handlers get called
  p.action(0);

  // (derived) object can be constructed on stack for simple cases
  // but in other (real) cases it is recommended that it is heap based.
  // so as not to have a naked ptr, it can be managed by a (special) shared
  // ptr that uses the GObject refcount for (shared) ownership tracking
  auto dp = gi::make_ref<Person, gi::construct_cpp_t>();
  // however, the GObject world has no knowledge of the subclass
  // (each instance is 1-to-1 with Person instance though).
  // so when we get something from that world, we can (sort-of dynamic)
  // cast to the subclass
  auto l = [](GObject_::Object ob, int id) {
    std::cout << " - triggered id " << id << std::endl;
    // obtain Person
    auto lp = gi::ref_ptr_cast<Person>(ob);
    if (lp)
      std::cout << " - it was a person!" << std::endl;
    // it really should be ...
    assert(lp);
  };
  dp->signal_trigger.connect(l);
  dp->action(1);

  // all of the above constructed Person in C++ centric style
  // that is, as part of the constructor (super class) execution,
  // a (derived) GObject is created and associated/extended with C++ side Person

  // alternatively, another derived GObject type can be registered,
  // which will trigger construction of a Person (as part of g_object_new()),
  // and then again associate those
  // the following essentially runs g_object_new()
  static int CUSTOM_AGE = 5;
  auto dpc = gi::make_ref<Person, gi::construct_c_t>("age", CUSTOM_AGE);
  // this will have a different GType
  assert(dpc->gobj_type_() != dp->gobj_type_());
  // as registered using specified name
  assert(g_type_from_name(PERSON_TYPE) == dpc->gobj_type_());
  // and the right age as specified in (gobject style) construct params
  assert(dpc->prop_age().get() == CUSTOM_AGE);

  // if the class supports C-style, it is selected automatically
  auto dpa = gi::make_ref<Person>();
  // should have ended up with C-style GType
  assert(dpa->gobj_type_() == dpc->gobj_type_());

  return 0;
}
