#ifndef GI_OBJECTCLASS_HPP
#define GI_OBJECTCLASS_HPP

/**
 * The purposes of the code and classes here is to perform registration of
 * a new GType type for a custom class that (suitably) inherits from these.
 * Setting up a new GType involves registration of a new type (and its signals
 * and properties).  This involves potentially custom overridden implementations
 * of the parent class(es) (C-style virtual) methods (= entries in class
 * structure) and likewise so for the interfaces.  For either of the latter
 * cases, pointers to wrapper functions are inserted in the class/interface
 * struct, that will in turn call generated C++ virtual methods
 * (which have presumably been overridden by the custom subclass).
 *
 * The tricky part in the above is that GType/C methods are "optional",
 * in that an entry in a class/interface struct can be left as NULL,
 * and it is a common pattern for calling code to check for such
 * (rather than unconditionally calling).  To preserve such behaviour,
 * an entry in a class/interface struct should only be "routed" to a
 * C++ virtual method if the custom SubClass actually has a definition for it.
 * The approach below tries to detect this (basically by checking if
 * SubClass::method is a different type than Base::method).  But that might
 * fail (with compilation errors due to overload resolution failure), so there
 * is also a system to manually specify what is defined, e.g. whether the
 * corresponding entry in the class/interface struct should be filled
 * for the SubClass' generated GType.
 *
 */

#include "callback.hpp"
#include "object.hpp"

GI_MODULE_EXPORT
namespace gi
{
// slightly nasty; will be generated
namespace repository
{
namespace GObject
{
enum class SignalFlags : std::underlying_type<::GSignalFlags>::type;
}
} // namespace repository

// specify construction type when creating ObjectClass based object
struct construct_t
{
  const int value;
  constexpr explicit construct_t(int v = 0) : value(v) {}
};
struct construct_auto_t : public construct_t
{
  constexpr construct_auto_t() : construct_t(0) {}
};
struct construct_cpp_t : public construct_t
{
  constexpr construct_cpp_t() : construct_t(1) {}
};
struct construct_c_t : public construct_t
{
  constexpr construct_c_t() : construct_t(2) {}
};

namespace detail
{
class ObjectBaseClass : public noncopyable
{
public:
  virtual ~ObjectBaseClass() {}

  ObjectBaseClass() : gobject_(nullptr) {}

  ObjectBaseClass(ObjectBaseClass &&other)
  {
    gobject_ = other.gobject_;
    other.gobject_ = nullptr;
  }

  ObjectBaseClass &operator=(ObjectBaseClass &&other)
  {
    if (this != &other) {
      gobject_ = other.gobject_;
      other.gobject_ = nullptr;
    }
    return *this;
  }

protected:
  typedef void (*interface_register_function)(GType class_type, gpointer init);
  void request_interface(interface_register_function reg, gpointer init)
  {
    itfs.emplace_back(reg, init);
  }

  GType base_gtype()
  {
    return gobject_ ? g_type_parent(G_OBJECT_TYPE(gobject_)) : G_TYPE_NONE;
  }

protected:
  // owns 1 ref (possibly managed externally)
  GObject *gobject_;
  // GType in case no gobject_
  GType gtype_;
  // additional type setup
  using interface_init_t = std::pair<interface_register_function, gpointer>;
  using interface_inits_t = std::vector<interface_init_t>;
  interface_inits_t itfs;
};

//// interface setup ////

// virtual inheritance as multiple inheritance will be used
// however, only 1 base instance should be around
class InterfaceClass : public virtual ObjectBaseClass
{
protected:
  static void add_interface(GType class_type, GType itf_type,
      GInterfaceInitFunc itf_init, gpointer init_data)
  {
    GInterfaceInfo itf_info = {
        itf_init, // interface_init
        nullptr,  // interface_finalize
        init_data // interface_data
    };

    g_type_add_interface_static(class_type, itf_type, &itf_info);
  }
};

template<typename ClassDef, typename SubClass>
gpointer forward_make_type_init_data();

template<typename InterfaceDef>
class InterfaceImpl : public InterfaceDef, public InterfaceClass
{
  typedef typename InterfaceDef::instance_type instance_type_t;

protected:
  static void register_interface(GType class_type, gpointer init_data)
  {
    add_interface(class_type, InterfaceDef::instance_type::get_type_(),
        InterfaceDef::interface_init, init_data);
  }

  // init data for registration-only use
  template<typename SubClass>
  static ObjectBaseClass::interface_init_t interface_init_data()
  {
    return {InterfaceImpl::register_interface,
        (gpointer)forward_make_type_init_data<InterfaceImpl, SubClass>()};
  }

  InterfaceImpl(gpointer init_data = nullptr)
  {
    request_interface(InterfaceImpl::register_interface, init_data);
  }

  // conversion to regular interface side
  instance_type_t interface_(gi::interface_tag<instance_type_t>)
  {
    return gi::wrap(
        (typename instance_type_t::BaseObjectType *)g_object_ref(gobject_),
        gi::transfer_full);
  }

  instance_type_t interface_()
  {
    return gi::wrap(
        (typename instance_type_t::BaseObjectType *)g_object_ref(gobject_),
        gi::transfer_full);
  }
};

template<typename InterfaceImpl>
class InterfaceClassImpl : public InterfaceImpl
{
  typedef typename InterfaceImpl::instance_type::BaseObjectType *c_type;
  typedef typename InterfaceImpl::interface_type interface_type_t;

  // use runtime data to avoid code generation of template
  // interface_type_t *istruct_;
  GType klass_;

protected:
  static interface_type_t *get_struct_(GType ktype)
  {
    auto klass = g_type_class_peek(ktype);
    return (interface_type_t *)g_type_interface_peek(
        klass, InterfaceImpl::instance_type::get_type_());
  }

  interface_type_t *get_struct_() { return get_struct_(klass_); }

  c_type gobj_() { return (c_type)this->gobject_; }

  InterfaceClassImpl(GType klass, gpointer itf_init_data = nullptr)
      : InterfaceImpl(itf_init_data), klass_(klass)
  {}
};

class ObjectClass;
class PropertyBase;

inline GQuark
object_data_quark()
{
  static const char *OBJECT_DATA_KEY = "GIOBJECT_OBJECT";
  static GQuark q = g_quark_from_static_string(OBJECT_DATA_KEY);
  return q;
}

using repository::GObject::Object;
using repository::GObject::ParamSpec;
using repository::GObject::SignalFlags;

//// init data collection ////

// a function pointer that serves as factory for the actual class_init data
// (e.g. holds data on which methods have been overridden)
// (void return to avoid function cast warning; returns real data otherwise)
typedef void (*type_init_data_factory_t)();
template<typename ClassDef, typename SubClass,
    typename std::enable_if<std::is_same<SubClass, void>::value>::type * =
        nullptr>
type_init_data_factory_t
make_type_init_data()
{
  return nullptr;
}

template<typename BaseDef, typename SubClass,
    typename std::enable_if<!std::is_same<SubClass, void>::value>::type * =
        nullptr>
type_init_data_factory_t
make_type_init_data()
{
  // hard cast; should return meaningful data
  return (type_init_data_factory_t)&BaseDef::TypeInitData::template factory<
      SubClass>;
}

template<typename ClassDef, typename SubClass>
gpointer
forward_make_type_init_data()
{
  return (gpointer)make_type_init_data<ClassDef, SubClass>();
}

// in generated code;
// each class/interface member (function) is assocated with a single type
// base class for tagged boolean member types
// (default to true for legacy case where no init data is captured from type)
template<typename Tag>
struct member_type
{
  bool value;
  constexpr member_type(bool v = true) : value(v) {}
  explicit operator bool() { return value; }
};

// combine manual Spec with code generated Default
template<typename Spec, typename Default>
struct Combine : public Spec, public Default
{
  using Default::has_definition;
  using Spec::defines;
  // dispatch from query signature to default if no manual specificiation
  template<typename MemberType, typename SubClass>
  constexpr static bool defines(const MemberType *m, const SubClass *cl)
  {
    return has_definition(m, cl);
  };
};

template<typename SubClass, typename Default>
using DefinitionData = Combine<typename SubClass::DefinitionData, Default>;

// see objectclass macros in gi_inc for how the above is used

//// class setup ////

// this collects properties that will be installed on a class type
// during the class_init (as opposed to added later on by property members)
// this is especially needed to install properties required by an interface
// (as those are checked by object_interface_check_properties early on)
typedef std::map<std::string, std::pair<PropertyBase *, ParamSpec>> properties;

template<typename T>
class property;

template<typename T, typename Base = repository::GObject::Object>
class signal;

struct ParamSpecInit : public ParamSpec
{
  cstring_v name_;
  ParamSpec spec_;

  // full spec arguments
  template<typename T, typename P, typename... Args,
      typename std::enable_if<sizeof...(Args) != 1>::type * = nullptr>
  ParamSpecInit(property<P> T::*, Args &&...args)
      : spec_(ParamSpec::new_<P>(std::forward<Args>(args)...))
  {}

  // name suffices for override property
  template<typename T, typename P>
  ParamSpecInit(property<P> T::*, const cstring_v name) : name_(name)
  {}
};

struct properties_init : public properties
{
  properties_init(std::initializer_list<ParamSpecInit> pd)
  {
    for (auto &&e : pd) {
      // should have either one
      auto name = e.name_;
      if (e.spec_)
        name = e.spec_.name_();
      insert({name, {nullptr, e.spec_}});
    }
  }
};

struct SignalSpec
{
  GType ret_type;
  std::vector<GType> types;
  gi::cstring_v name;
  SignalFlags flags;

  template<typename T, typename R, typename Instance, typename... Args>
  SignalSpec(signal<R(Instance, Args...)> T::*, const gi::cstring_v _name,
      SignalFlags _flags = (SignalFlags)0)
      : ret_type(gi::traits::gtype<R>::get_type()),
        types({gi::traits::gtype<Args>::get_type()...}), name(_name),
        flags(_flags)
  {}
};

using SignalSpecInit = SignalSpec;

using signals = std::vector<SignalSpec>;
using signals_init = signals;

inline void class_init_props_sigs(
    ObjectClass *impl, gpointer g_class, gpointer props, gpointer sigs);

inline gi::cstring_v
klass_type_name(const std::type_info &ti, gi::cstring_v klassname)
{
  return (klassname && klassname.at(0)) ? klassname : ti.name();
}

template<typename T, typename Enable = void>
struct custom_class_init : public std::false_type
{
  static constexpr GClassInitFunc value = nullptr;
};

template<typename T>
struct custom_class_init<T,
    typename traits::if_valid_type<decltype(T::custom_class_init)>::type>
    : public std::true_type
{
  static constexpr auto value = T::custom_class_init;
};

template<typename T, typename Enable = void>
struct custom_instance_init : public std::false_type
{
  static constexpr GInstanceInitFunc value = nullptr;
};

template<typename T>
struct custom_instance_init<T,
    typename traits::if_valid_type<decltype(T::custom_init)>::type>
    : public std::true_type
{
  static constexpr auto value = T::custom_init;
};

class ObjectClass : public virtual ObjectBaseClass
{
public:
  typedef gi::repository::GObject::Object instance_type;
  typedef GObjectClass class_type;
  // above init code refers to this inner type unconditionally
  // so arrange for a fallback in baseclass
  struct DefinitionData
  {
    constexpr static bool defines(...) { return false; }
  };

  // used as single argument in constructor for c-first style subclass
  struct InitData
  {
    mutable Object instance;

    explicit operator bool() const { return instance.gobj_(); }

  protected:
    // only for internal use
    // and also complicates calling any function/constructor using this type
    // silly dummy argument prevents ambiguity in ObjectImpl constructors
    // when specifying all {}
    explicit InitData(int) {}
    friend class ObjectClass;
  };

protected:
  struct ClassInitNode
  {
    GClassInitFunc self;
    type_init_data_factory_t class_init_data_factory;
    const ClassInitNode *child;
  };

private:
  static const constexpr char *CLASS_PREFIX = "GIOBJECT__";

  static std::string canonical_name(const std::string &name)
  {
    auto result = name;
    for (auto &p : result) {
      if (!(g_ascii_isalnum(p) || p == '_' || p == '-'))
        p = '+';
    }
    return result;
  }

  typedef void (*GObjectConstructed)(GObject *object);
  typedef GObject *(*GObjectConstructor)(GType type,
      guint n_construct_properties,
      GObjectConstructParam *construct_properties);

  typedef std::vector<std::pair<GClassInitFunc, type_init_data_factory_t>>
      class_inits_t;
  struct class_data_t
  {
    std::unique_ptr<class_inits_t> class_inits;
    GClassInitFunc custom_class_init;
    const properties *props;
    const signals *sigs;
    ObjectClass *impl;
    GObjectConstructor constructor;
  };

  struct custom_inits_t
  {
    GClassInitFunc custom_class_init{};
    GInstanceInitFunc custom_init{};
    GObjectConstructor constructor{};
    GObjectConstructed constructed{};
  };

  static void class_init_all(gpointer g_class, gpointer class_data)
  {
    std::unique_ptr<class_data_t> data((class_data_t *)class_data);

    // class_init below is called with top-level class_data,
    // others (= code generated) with their own
    for (auto &&ci : *data->class_inits)
      ci.first(
          g_class, ci.first == &class_init ? class_data : gpointer(ci.second));
  }

  static void class_init(gpointer g_class, gpointer class_data)
  {
    class_data_t *data = ((class_data_t *)class_data);
    // delegate property handling
    class_init_props_sigs(
        data->impl, g_class, (gpointer)data->props, (gpointer)data->sigs);
    // set constructed to invoke constructor
    if (data->constructor)
      ((GObjectClass *)(g_class))->constructor = data->constructor;
    // also call top-level class custom class init
    if (data->custom_class_init) {
      data->custom_class_init(g_class, nullptr);
    }
  }

  GType register_type(GType base_type, const gi::cstring_v klassname,
      const ClassInitNode &init_node, const interface_inits_t &itfs,
      const properties &props, const signals &sigs, custom_inits_t custom_inits)
  {
    // nothing to do if already registered
    GType custom_type = g_type_from_name(klassname.c_str());
    if (custom_type)
      return custom_type;

    // otherwise create with same class/instance size as parent type
    GTypeQuery base_query = {
        0,
        nullptr,
        0,
        0,
    };
    g_type_query(base_type, &base_query);

    const guint16 class_size = (guint16)base_query.class_size;
    const guint16 instance_size = (guint16)base_query.instance_size;

    // collect chain of class inits
    std::unique_ptr<class_inits_t> class_inits(new class_inits_t());
    auto node = &init_node;
    while (node) {
      if (node->self)
        class_inits->push_back({node->self, node->class_init_data_factory});
      node = node->child;
    }

    // assemble class_data
    std::unique_ptr<class_data_t> class_data(new class_data_t());
    // class creation will be triggered upon instance creation
    // which will happen shortly after this class
    // (so the list has to handled special, but the others will still be
    // around)
    class_data->class_inits = std::move(class_inits);
    class_data->custom_class_init = custom_inits.custom_class_init;
    class_data->impl = this;
    class_data->props = &props;
    class_data->sigs = &sigs;
    class_data->constructor = custom_inits.constructor;

    const GTypeInfo derived_info = {
        class_size,
        nullptr, // base_init
        nullptr, // base_finalize
        class_init_all,
        nullptr,              // class_finalize
        class_data.release(), // class_data
        instance_size,
        0,                        // n_preallocs
        custom_inits.custom_init, // instance_init
        nullptr,                  // value_table
    };

    custom_type = g_type_register_static(
        base_type, klassname.c_str(), &derived_info, GTypeFlags(0));

    // handle interfaces
    for (auto &&itf : itfs)
      itf.first(custom_type, itf.second);

    // force/finish class creation,
    // so a subsequent _peek does not return NULL
    // (and lists above are still around)
    g_type_class_unref(g_type_class_ref(custom_type));

    return custom_type;
  }

  // minor convenience wrap for the above
  GType register_type(GType parent, const gi::cstring_v klassname,
      const ClassInitNode *node, const properties &props, const signals &sigs,
      custom_inits_t custom_inits)
  {
    auto gtype = register_type(parent, klassname, {class_init, nullptr, node},
        itfs, props, sigs, custom_inits);
    itfs.clear();
    return gtype;
  }

  void setup_instance(GType gtype,
      const repository::GObject::construct_params &params,
      gpointer instance = nullptr)
  {
    // not good if provided instance already has associated C++ instance
    g_return_if_fail(!instance || !ObjectClass::instance((GObject *)instance));
    g_return_if_fail(gtype);

    // no longer needed
    itfs.clear();
    // create and link object instance
    // if needed, that is, otherwise use provided instance and tie onto that one
    gtype_ = gtype;
    GObject *obj = gobject_ =
        (GObject *)(instance ? instance : Object::new_(gtype, params));
    // should still be floating, then we assume ownership
    // if it is no longer, then it has already been stolen (e.g. GtkWindow),
    // and we need to add one here
    if (!instance && g_type_is_a(gtype, G_TYPE_INITIALLY_UNOWNED))
      g_object_ref_sink(gobject_);
    // mark this as associated wrapper object as retrieved by .instance()
    g_object_set_qdata_full(obj, object_data_quark(), this, destroy_notify);
  }

  // C++ side construction
  // always (try to) register type and create instance
  template<typename SubClass>
  void register_setup(GType parent, const gi::cstring_v klassname,
      const ClassInitNode *node,
      const repository::GObject::construct_params &params,
      const properties &props, const signals &sigs, custom_inits_t custom_inits,
      gpointer instance = nullptr, std::nullptr_t = nullptr)
  {
    // sort-of internal prefixed klassname
    auto custom_name = std::string(CLASS_PREFIX) + canonical_name(klassname);
    auto gtype =
        register_type(parent, custom_name, node, props, sigs, custom_inits);
    setup_instance(gtype, params, instance);
  }

  template<typename SubClass>
  static GObject *instance_constructor(GType type, guint n_construct_properties,
      GObjectConstructParam *construct_properties)
  {
    // chain up, but we can skip other parent variations of this function
    auto gtype = SubClass::baseclass_type::get_type_();
    auto klass = G_OBJECT_CLASS(g_type_class_peek(gtype));
    auto instance =
        klass->constructor(type, n_construct_properties, construct_properties);

    g_assert(instance);
    // there should be no C++ side yet
    // as we chain up past any possible C++ parent
    g_assert(!ObjectClass::instance(instance));

    // handle C++ setup
    auto floating = g_object_is_floating(instance);
    // avoid inadvertent sink
    InitData id{0};
    id.instance = gi::wrap((GObject *)g_object_ref(instance), transfer_full);
#if GI_CONFIG_EXCEPTIONS
    try {
#endif
      auto self = new SubClass(id);
      // sanity check on ref
      if (floating && !g_object_is_floating(instance)) {
        g_warning("%s constructor sinks instance", typeid(SubClass).name());
        // try to unsink if it seems safe and applicable
        auto obj = (GObject *)instance;
        // theoretically not MT safe, but if == 1, only 1 thread should be
        // involved
        if (obj->ref_count == 1) {
          g_warning("re-floating instance");
          g_object_force_floating(obj);
        }
      }
      (void)self;
      // self->setup is essentially run below at bottom of constructor chain)
      // this will then assign ownership of self to instance
      g_assert(self->gobj_() == (gpointer)instance);
#if GI_CONFIG_EXCEPTIONS
    } catch (const std::exception &exc) {
      // bad things will happen
      report_exception(exc);
      g_critical("constructor failed in instance_init");
    }
#endif

    return instance;
  }

  // C-side construction
  // either register type, or finish setup of an instance (triggered on C side_
  template<typename SubClass>
  void register_setup(GType parent, const gi::cstring_v klassname,
      const ClassInitNode *node,
      const repository::GObject::construct_params &params,
      const properties &props, const signals &sigs, custom_inits_t custom_inits,
      gpointer instance = nullptr, const InitData *id = {})
  {
    g_assert(id);
    g_assert(!instance);
    // no real instance, so used for type registration purpose
    if (!id->instance) {
      // these parts are not applicable
      (void)instance;
      (void)params;
      g_assert(!instance);
      g_assert(params.empty());
      // we have our own custom init, which creates the cpp object
      // so the latter's constructor serves as custom init
      // custom_inits.custom_init = instance_init<SubClass>;
      custom_inits.constructor = instance_constructor<SubClass>;
      // there is no gobject_ instance
      // make sure to track type for subsequent signal and property registration
      gtype_ =
          register_type(parent, klassname, node, props, sigs, custom_inits);
      g_assert(gtype_);
    } else {
      // so this is a new instance as created by instance_init
      // ensure instance association here at the end of constructor chain
      // so it is that way for the sequel of the subclass constructor
      // (and redundant signal and property registration)
      instance = id->instance.gobj_();
      setup_instance(G_OBJECT_TYPE(instance), {}, instance);
    }
  }

  static void destroy_notify(gpointer data)
  {
    ObjectClass *impl = (ObjectClass *)data;
    // sever link with object instance
    impl->gobject_ = nullptr;
    delete impl;
  }

protected:
  template<typename SubClass>
  static custom_inits_t make_custom_inits()
  {
    return {GClassInitFunc(custom_class_init<SubClass>::value),
        GInstanceInitFunc(custom_instance_init<SubClass>::value)};
  }

  ObjectClass(GType parent, const gi::cstring_v klassname,
      const ClassInitNode &node,
      const repository::GObject::construct_params &params,
      const properties &props)
  {
    register_setup<void>(
        parent, klassname, &node, params, props, {}, {}, nullptr, nullptr);
  }

  template<typename SubClass, typename InitData>
  ObjectClass(const SubClass *, GType parent, const gi::cstring_v klassname,
      const ClassInitNode &node,
      const repository::GObject::construct_params &params,
      const properties &props, gpointer instance, InitData id)
  {
    register_setup<SubClass>(parent, klassname, &node, params, props, {},
        make_custom_inits<SubClass>(), instance, id);
  }

  template<typename SubClass>
  ObjectClass(const SubClass *, const gi::cstring_v klassname, GType base,
      const ObjectClass::ClassInitNode &node, const interface_inits_t &itfs,
      const properties &props, const signals &sigs)
  {
    this->itfs = itfs;
    auto id = InitData{0};
    register_setup<SubClass>(base, klassname, &node, {}, props, sigs,
        make_custom_inits<SubClass>(), nullptr, &id);
  }

  ~ObjectClass()
  {
    // object destruction should typically be initiated from the
    // associated object instance based on refcount,
    // and so pass through destroy_notify, in which case no more gobject_
    // but it could come here first for a stack based custom object or alike
    if (gobject_) {
      // corresponding object should not have outstanding refs out there
      // not good otherwise, and why it should not be destructed this way
      if (gobject_->ref_count != 1)
        g_error("destroying object with outstanding object refs");
      // NOTE the unref might still trigger vmethod calls,
      // but destruction already happened down to this level,
      // so derived cast and vmethod call no longer possible
      // so sever link anyway to make that clear
      g_object_steal_qdata(gobject_, object_data_quark());
      g_object_unref(gobject_);
    }
  }

public:
  ObjectClass(const std::type_info &ti,
      const repository::GObject::construct_params &params = {},
      const properties &props = {})
  {
    register_setup<void>(instance_type::get_type_(), ti.name(), nullptr, params,
        props, {}, {}, nullptr, nullptr);
  }

  template<typename SubClass, typename InitData = std::nullptr_t>
  ObjectClass(const SubClass *,
      const repository::GObject::construct_params &params = {},
      const properties &props = {}, gpointer instance = nullptr,
      const gi::cstring_v klassname = nullptr, InitData id = nullptr)
  {
    const auto &ti = typeid(SubClass);
    register_setup<SubClass>(instance_type::get_type_(),
        klass_type_name(ti, klassname), nullptr, params, props, {},
        make_custom_inits<SubClass>(), instance, id);
  }

  operator Object() { return gi::wrap(gobject_, transfer_none); }

  GType gobj_klass_type()
  {
    // there should almost always be an object instance
    // except during initial registration of c-first style
    return gobject_ ? G_OBJECT_TYPE(gobject_) : gtype_;
  }

  GObjectClass *gobj_klass()
  {
    return (GObjectClass *)g_type_class_peek(gobj_klass_type());
  }

  static ObjectClass *instance(GObject *gobject)
  {
    return (ObjectClass *)g_object_get_qdata(gobject, object_data_quark());
  }

private:
  template<typename T>
  friend GType register_type();

  template<typename T>
  static GType register_type_()
  {
    // check if the class has defined a get_type_()
    // rather than nifty compile-time checks, simply use runtme
    auto btype = T::baseclass_type::get_type_();
    auto gtype = T::get_type_();
    if (gtype == btype) {
      // so nothing happened, use constructor to register
      // instantiate dummy instance that registers type
      gtype = (T{InitData{0}}).gobj_klass_type();
    }
    return gtype;
  }
};

// interfaces need to go left; constructors need to run first (to request
// interface) class constructor deals with class_init last
template<typename ClassDef, typename BaseClass, typename... Interfaces>
class ClassTemplate : public ClassDef, public Interfaces..., public BaseClass
{
  typedef typename ClassDef::instance_type::BaseObjectType *c_type;
  typedef typename ClassDef::class_type class_type_t;
  typedef typename ClassDef::instance_type instance_type_t;

  // make private
  using ClassDef::class_init;

protected:
  class_type_t *get_struct_()
  {
    return (class_type_t *)g_type_class_peek(this->base_gtype());
  }

  c_type gobj_() { return (c_type)this->gobject_; }

  // constructor to be used by custom subclass
  [[deprecated]] ClassTemplate(const std::type_info &ti,
      const repository::GObject::construct_params &params = {},
      const properties &props = {})
      : Interfaces(instance_type_t::get_type_())...,
        BaseClass(instance_type_t::get_type_(), ti.name(),
            {&ClassDef::class_init, nullptr, nullptr}, params, props)
  {}

  // constructor for inner inheritance chain
  [[deprecated]] ClassTemplate(GType base, const gi::cstring_v klassname,
      const ObjectClass::ClassInitNode &node,
      const repository::GObject::construct_params &params,
      const properties &props)
      : Interfaces(instance_type_t::get_type_())...,
        BaseClass(base, klassname, {&ClassDef::class_init, nullptr, &node},
            params, props)
  {}

  // as above, new style
  // constructor to be used by custom subclass
  template<typename SubClass, typename InitData = std::nullptr_t>
  ClassTemplate(const SubClass *sub,
      const repository::GObject::construct_params &params = {},
      const properties &props = {}, gpointer instance = nullptr,
      const gi::cstring_v klassname = nullptr, InitData id = nullptr)
      : Interfaces(instance_type_t::get_type_(),
            gpointer(make_type_init_data<Interfaces, SubClass>()))...,
        BaseClass(sub, instance_type_t::get_type_(),
            klass_type_name(typeid(SubClass), klassname),
            {&ClassDef::class_init, make_type_init_data<ClassDef, SubClass>(),
                nullptr},
            params, props, instance, id)
  {}

  // constructor for inner inheritance chain
  template<typename SubClass, typename InitData = std::nullptr_t>
  ClassTemplate(const SubClass *sub, GType base, const gi::cstring_v klassname,
      const ObjectClass::ClassInitNode &node,
      const repository::GObject::construct_params &params,
      const properties &props, gpointer instance = nullptr,
      InitData id = nullptr)
      : Interfaces(instance_type_t::get_type_(),
            gpointer(make_type_init_data<Interfaces, SubClass>()))...,
        BaseClass(sub, base, klassname,
            {&ClassDef::class_init, make_type_init_data<ClassDef, SubClass>(),
                &node},
            params, props, instance, id)
  {}

  // constructor used in inner inheritance chain for registration collection
  template<typename SubClass>
  ClassTemplate(const SubClass *sub, const gi::cstring_v klassname, GType base,
      const ObjectClass::ClassInitNode &node,
      const typename ClassTemplate::interface_inits_t &itfs,
      const properties &props, const signals &sigs)
      : Interfaces(instance_type_t::get_type_(),
            gpointer(make_type_init_data<Interfaces, SubClass>()))...,
        BaseClass(sub, klassname, base,
            {&ClassDef::class_init, make_type_init_data<ClassDef, SubClass>(),
                &node},
            itfs, props, sigs)
  {}

public:
  class_type_t *gobj_klass() { return (class_type_t *)BaseClass::gobj_klass(); }

  // repeat to disambiguate
  typedef typename ClassDef::instance_type instance_type;

  // access to regular object side
  instance_type_t object_()
  {
    auto obj = this->gobject_ ? g_object_ref(this->gobject_) : nullptr;
    return gi::wrap(
        (typename instance_type_t::BaseObjectType *)(obj), gi::transfer_full);
  }
};

// NOTE as the impl and regular object side are both inherited here,
// ambiguity might not only result from inheriting multiple interfaces,
// but also between either of these sides
// as such, no operator cast is added on the impl side
// instead, use the object_() member to pass to/through regular side
template<typename ObjectT, typename ClassT>
class ObjectImpl : public ObjectT, public ClassT
{
public:
  typedef typename ClassT::instance_type baseclass_type;
  using ObjectT::gobj_;

protected:
  [[deprecated]] ObjectImpl(const std::type_info &ti,
      const repository::GObject::construct_params &params =
          repository::GObject::construct_params{},
      const properties &props = properties{})
      : ClassT(ti, params, props)
  { // link object ptrs (untracked by ObjectBase)
    this->data_ = this->gobject_;
  }

  // NOTE only 1 gtype will be registered,
  // so all (subclass) constructors should specify consistent/same data
  template<typename SubClass>
  ObjectImpl(const SubClass *sub,
      const repository::GObject::construct_params &params =
          repository::GObject::construct_params{},
      const properties &props = properties{})
      : ClassT(sub, params, props)
  { // link object ptrs (untracked by ObjectBase)
    this->data_ = this->gobject_;
  }

  // special advanced case (for internal/override use) by custom subclass
  // where constructed instance is associated with provided object instance
  // (rather than the latter created as part of construction, as usual)
  // if klassname KlassName is specified,
  // registered typename is GIOBJECT__KlassName
  // CAUTION the approach below is more likely applicable
  template<typename SubClass>
  ObjectImpl(ObjectT instance, const SubClass *sub,
      const gi::cstring_v klassname = nullptr,
      const repository::GObject::construct_params &params =
          repository::GObject::construct_params{},
      const properties &props = properties{})
      : ClassT(sub, params, props, instance.gobj_(), klassname)
  { // link object ptrs (untracked by ObjectBase)
    this->data_ = this->gobject_;
  }

  // this will either;
  // + register an object GType with an instance_init that
  //   new()'s a corresponding cpp object (and associates suitably)
  //   so, it can be safely created based on GType (e.g. by some C-factory)
  //   (InitData is essentially empty in this case,
  //    and the cpp ObjectImpl instance is a transient dummy)
  // + construct instance invoked as part of the aforementioned new()
  //   (InitData then holds C object instance)
  // registered type is klassname (as-is)
  template<typename SubClass>
  ObjectImpl(const SubClass *sub, const ObjectClass::InitData &id,
      const gi::cstring_v klassname = {},
      const properties &props = properties{})
      : ClassT(sub, {}, props, nullptr, klassname, &id)
  { // should have a name if this is used to register a type
    g_return_if_fail(id || !klassname.empty());
    // link object ptrs (untracked by ObjectBase)
    this->data_ = this->gobject_;
  }

  ~ObjectImpl()
  { // disconnect (avoid ObjectBase management)
    this->data_ = nullptr;
  }

  // registers a type (instead of using that part of the constructor above)
  // if parent is 0, then it defaults to the type of the immediate parent
  // (so it should only be specified if subclass'ing a subclass)
  // itfs: use {I::interface_init_data(), ...} for *immediate* parents I,
  //    *not* parent interfaces of parents
  // props, signals: use succinct list-initialization (as used elsewhere)
  template<typename SubClass>
  static GType register_type_(const gi::cstring_v klassname, GType parent,
      const typename ClassT::interface_inits_t &itfs,
      const properties_init &props, const signals_init &sigs)
  {
    // collect all data using constructor chain and register type
    // MT-safe according to C++11 specs
    static GType gtype =
        ObjectImpl((SubClass *)nullptr, klassname, parent, itfs, props, sigs)
            .gobj_klass_type();
    return gtype;
  }

private:
  // circumvent protected constructors/destructors
  template<typename SubClass>
  ObjectImpl(const SubClass *sub, const gi::cstring_v klassname, GType parent,
      const typename ClassT::interface_inits_t &itfs,
      const properties_init &props, const signals_init &sigs)
      : ClassT(sub, klassname, parent ? parent : baseclass_type::get_type_(),
            {nullptr, nullptr, nullptr}, itfs, props, sigs)
  {}
};

// wrapper helper to call virtual method
// used in implementation, so we can casually use types in default argument
template<typename C, typename T, typename RetTransfer,
    typename ArgTransfers = void,
    typename CSig = typename map_cpp_function<T,
        typename std::conditional<std::is_null_pointer<RetTransfer>::value, T,
            void>::type>::type>
struct method_wrapper;

template<typename C, typename R, typename... Args, typename RetTransfer,
    typename... Transfers, typename CR, typename... CArgs>
struct method_wrapper<C, R (*)(Args...), RetTransfer, std::tuple<Transfers...>,
    CR(CArgs...)>
{
private:
  typedef R (C::*member_type)(Args...);

  struct caller_data
  {
    C *this_;
    const member_type m;
  };
  static R caller(Args &&...args, void *user_data)
  {
    auto d = (caller_data *)user_data;
    return ((d->this_)->*(d->m))(std::forward<Args>(args)...);
  }

public:
  template<member_type m>
  static CR wrapper(
      typename traits::ctype<typename C::instance_type>::type p, CArgs... args)
  {
    ObjectClass *oc = ObjectClass::instance((GObject *)p);
    C *c = dynamic_cast<C *>(oc);
    if (!oc) {
      // connection already severed by heap destruction
      // use refptr instead of stack allocation if this is a problem
      g_error("missing object");
    } else if (!c) {
      // on our way to crash anyway
      g_error("wrong object type");
    }
    caller_data d{c, m};
    return transform_caller<R(Args...), RetTransfer, std::tuple<Transfers...>,
        CR(CArgs...)>::wrapper(args..., caller, &d);
  }
};

// simplified special case for plain/raw fallback with no wrapping/transfer
// (pick std::nullptr_t for specialization to represent absence of transfer)
template<typename C, typename R, typename... Args>
struct method_wrapper<C, R (*)(Args...), std::nullptr_t>
{
private:
  typedef R (C::*member_type)(Args...);

public:
  template<member_type m>
  static R wrapper(
      typename traits::ctype<typename C::instance_type>::type p, Args... args)
  {
    ObjectClass *oc = ObjectClass::instance((GObject *)p);
    C *c = dynamic_cast<C *>(oc);
    if (!oc) {
      // connection already severed by heap destruction
      // use refptr instead of stack allocation if this is a problem
      g_error("missing object");
    } else if (!c) {
      // on our way to crash anyway
      g_error("wrong object type");
    }
    return ((c)->*(m))(std::forward<Args>(args)...);
  }
};

//// property handling ////

class PropertyBase
{
  typedef PropertyBase self_type;

  // subclass handles this
  virtual void set_property(const GValue *value) = 0;
  virtual void get_property(GValue *value) = 0;

  static GQuark get_instance_quark(guint prop_id)
  {
    auto str = std::string("GI__") +
               std::to_string((unsigned long)(&class_init)) + '_' +
               std::to_string(prop_id);
    return g_quark_from_string(str.c_str());
  }

  static PropertyBase *get_instance(
      GObject *object, GParamSpec *pspec, guint prop_id)
  {
    auto impl = ObjectClass::instance(object);
    auto poffset = g_param_spec_get_qdata(pspec, get_instance_quark(prop_id));
    return poffset ? (PropertyBase *)((char *)impl + GPOINTER_TO_INT(poffset))
                   : nullptr;
  }

  static void get_property(
      GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
  {
    auto prop = get_instance(object, pspec, prop_id);
    if (prop) {
      prop->get_property(value);
    } else {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
  }

  static void set_property(
      GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
  {
    auto prop = get_instance(object, pspec, prop_id);
    if (prop) {
      prop->set_property(value);
    } else {
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
  }

protected:
  static GQuark get_prop_quark(const char *name)
  {
    auto str = std::string("GI__") + name;
    return g_quark_from_string(str.c_str());
  }

public:
  static void install_property(ObjectClass *impl, GObjectClass *klass,
      self_type *self, ParamSpec pspec, const gi::cstring_v name = "")
  {
    g_return_if_fail(impl);
    g_return_if_fail(klass);
    // need at least 1 parameter
    g_return_if_fail(pspec || name.size());

    // identify any property tracked by this PropertyBase code
    // (may or may not be unique process-wise)
    static guint prop_id;

    auto pname = name;
    if (pspec) {
      // mind transfer full
      g_object_class_install_property(klass, ++prop_id, pspec.gobj_copy_());
      // normalize name
      pname = pspec.name_();
    } else {
      g_object_class_override_property(klass, ++prop_id, name.c_str());
    }

    // mark property installed on this klass/type (by whatever code path)
    // sadly, in case of overridden properties,
    // glib does not expose sufficient info on whether it is already further up
    // or the override that we may be trying to add to this/our class
    // so, setup some parallel tracking using qdata
    auto gtype = G_OBJECT_CLASS_TYPE(klass);
    g_type_set_qdata(
        gtype, get_prop_quark(pname.c_str()), GINT_TO_POINTER(prop_id));

    // self may be absent if only registering type
    if (self) {
      install_property_offset(impl, klass, self, pname, prop_id);
    }
  }

  static void install_property_offset(ObjectClass *impl, GObjectClass *klass,
      self_type *self, const gi::cstring_v name, guint prop_id)
  {
    g_return_if_fail(self);

    // add metadata to pspec to retrieve upon set/get
    // an overridden one will be passed to get/set
    // (and returned by find as well)
    auto gpspec = g_object_class_find_property(klass, name.c_str());
    g_assert(gpspec);
    auto quark = get_instance_quark(prop_id);
    // NOTE no race/issue if multiple threads set this (to same value)
    // (internal locks are used by glib)
    if (!g_param_spec_get_qdata(gpspec, quark)) {
      auto offset = ((char *)self - (char *)impl);
      g_param_spec_set_qdata(gpspec, quark, GINT_TO_POINTER(offset));
    }
  }

  virtual ~PropertyBase() {}

  static void class_init(
      ObjectClass *impl, gpointer g_class, gpointer class_data)
  {
    auto klass = (GObjectClass *)g_class;
    auto props = (properties *)class_data;

    klass->get_property = get_property;
    klass->set_property = set_property;

    // need to add override properties at this point
    // (before creation of any instance or class struct)
    for (auto &&e : *props)
      install_property(impl, klass, e.second.first, e.second.second, e.first);
  }
};

inline void
class_init_props_sigs(
    ObjectClass *impl, gpointer g_class, gpointer props, gpointer _sigs)
{
  g_return_if_fail(g_class);

  if (props)
    PropertyBase::class_init(impl, g_class, props);

  // handle signals right here
  if (_sigs) {
    auto sigs = (signals *)_sigs;
    for (auto &sig : *sigs) {
      auto gtype = G_OBJECT_CLASS_TYPE(g_class);
      g_signal_newv(sig.name.c_str(), gtype, (GSignalFlags)sig.flags, nullptr,
          nullptr, nullptr, nullptr, sig.ret_type, sig.types.size(),
          sig.types.data());
    }
  }
}

template<typename T>
class property : protected property_proxy<T>, public PropertyBase
{
  T val_;

  void add_property(ObjectClass *impl, gi::cstring_v name)
  {
    auto pspec = Object::find_property(impl->gobj_klass_type(), name);
    // could have been defined already upon prior object creation
    if (pspec) {
      this->pspec_ = pspec;
      // normalize name
      name = pspec.name_();
      // could be defined by a superclass
      // or already added to this class by prior instance
      auto gtype = G_OBJECT_CLASS_TYPE(impl->gobj_klass());
      if (auto pd = g_type_get_qdata(gtype, get_prop_quark(name.c_str()))) {
        install_property_offset(
            impl, impl->gobj_klass(), this, name, GPOINTER_TO_INT(pd));
      } else {
        install_property(impl, impl->gobj_klass(), this, nullptr, name);
      }
    } else {
      pspec = this->pspec_;
      install_property(impl, impl->gobj_klass(), this, pspec);
    }
    // set value to default param value
    Value value;
    value.init<T>();
    g_param_value_set_default(pspec.gobj_(), &value);
    val_ = detail::get_value<T>(&value);
    // avoid circular ref loop
    if (this->object_)
      g_object_unref(this->object_.gobj_());
  }

protected:
  void set_property(const GValue *value) override
  {
    val_ = detail::get_value<T>(value);
  }

  void get_property(GValue *value) override { detail::set_value(value, val_); }

public:
  template<typename... Args,
      typename std::enable_if<sizeof...(Args) != 1>::type * = nullptr>
  property(ObjectClass *impl, Args &&...args)
      : property_proxy<T>(
            (Object)(*impl), ParamSpec::new_<T>(std::forward<Args>(args)...))
  {
    add_property(impl, this->pspec_.get_name());
  }

  property(ObjectClass *impl, const gi::cstring_v name)
      : property_proxy<T>((Object)(*impl), ParamSpec())
  {
    add_property(impl, name);
  }

  ~property()
  {
    // clear link
    this->object_.release_();
  }

  void notify()
  {
    g_object_notify_by_pspec(this->object_.gobj_(), this->pspec_.gobj_());
  }

  PropertyBase &operator=(T value)
  {
    val_ = value;
    notify();
    return *this;
  }

  void set_value(T value, bool _notify = true)
  {
    val_ = value;
    if (_notify)
      notify();
  }

  T get_value() const { return val_; }

  operator T() const { return val_; }

  property_proxy<T> get_proxy() const { return *this; }
};

template<typename T>
class property_read : public property<T>
{
public:
  template<typename... Args>
  property_read(ObjectClass *impl, Args &&...args)
      : property<T>(
            impl, std::forward<Args>(args)..., ParamFlags(G_PARAM_READABLE))
  {}

  property_proxy_read<T> get_proxy() const
  {
    return {this->object_, this->pspec_};
  }
};

template<typename T>
class property_write : public property<T>
{
public:
  template<typename... Args>
  property_write(ObjectClass *impl, Args &&...args)
      : property<T>(
            impl, std::forward<Args>(args)..., ParamFlags(G_PARAM_WRITABLE))
  {}

  property_proxy_write<T> get_proxy() const
  {
    return {this->object_, this->pspec_};
  }
};

//// signal handling ////

template<typename R, typename Instance, typename... Args, typename Base>
class signal<R(Instance, Args...), Base>
    : public signal_proxy<R(Instance, Args...)>
{
  typedef signal_proxy<R(Instance, Args...)> super;

  static guint new_(const gi::cstring_v name, GType itype, SignalFlags flags)
  {
    // collect array of types
    GType types[] = {traits::gtype<Args>::get_type()...};
    const int nparams = sizeof...(Args);
    const GType ret_type = traits::gtype<R>::get_type();
    // TODO accumulator
    return g_signal_newv(name.c_str(), itype, (GSignalFlags)flags, nullptr,
        nullptr, nullptr, nullptr, ret_type, nparams, types);
  }

public:
  signal(ObjectClass *owner, const gi::cstring_v name,
      SignalFlags flags = (SignalFlags)0)
      : super(*owner, name)
  {
    const GType itype = owner->gobj_klass_type();
    if (!g_signal_lookup(name.c_str(), itype))
      new_(name, itype, flags);
    // sneak away ref to avoid ref loop
    if (this->object_)
      g_object_unref(this->object_.gobj_());
  }

  ~signal()
  {
    // clear link
    this->object_.release_();
  }
};

template<typename T>
struct ObjectDeleter
{
  void operator()(T *ob)
  {
    if (ob->gobj_())
      g_object_unref(ob->gobj_());
  }
};

template<typename T>
class ref_ptr : public std::unique_ptr<T, ObjectDeleter<T>>
{
  typedef std::unique_ptr<T, ObjectDeleter<T>> super;
  typedef typename T::baseclass_type baseclass_type;

public:
  ref_ptr(T *ptr = nullptr, bool own = true) : super(ptr)
  {
    if (ptr && !own)
      g_object_ref(ptr->gobj_());
  }

  ref_ptr(ref_ptr &&other) = default;
  ref_ptr(const ref_ptr &other) : super(nullptr)
  {
    if (other)
      g_object_ref(other->gobj_());
    this->reset(other.get());
  }

  ref_ptr &operator=(ref_ptr &&other) = default;
  ref_ptr &operator=(const ref_ptr &other)
  {
    if (other && &other != this)
      g_object_ref(other->gobj_());
    this->reset(other.get());
    return *this;
  }

  operator baseclass_type() { return *this->get(); }
};

template<typename T, typename... Args>
ref_ptr<T>
make_ref_tagged(construct_cpp_t, Args &&...args)
{
  // move ownership of ref acquired during creation
  return ref_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T, typename traits::if_valid_type<
                         typename T::baseclass_type>::type * = nullptr>
ref_ptr<T>
ref_ptr_cast(Object ob)
{
  if (ob) {
    ObjectClass *instance = ObjectClass::instance(ob.gobj_());
    if (instance) {
      auto obj = dynamic_cast<T *>(instance);
      if (obj) {
        // arrange to obtain an extra ref
        return ref_ptr<T>(obj, false);
      }
    }
  }
  return nullptr;
}

// similar to a typical glib xyz_get_type
// as both register the type (once) and return it (many times)
template<typename T>
GType
register_type()
{
  // C++11 standard specifies this should be MT-safe and occur only once
  // as such similar to the typical g_once in xyz_get_type
  static GType gtype = ObjectClass::register_type_<T>();
  return gtype;
}

template<typename T, typename... Args>
ref_ptr<T>
make_ref_tagged(construct_c_t, Args &&...args)
{
  auto gtype = register_type<T>();
  auto obj = Object::new_<::GObject *>(gtype, std::forward<Args>(args)...);
  return ref_ptr_cast<T>(obj);
}

// Construct can be specified explicitly;
// construct_c_t; also selected in auto case if supported,
//    Args specify construct properties (see also GObject::new_)
// construct_cpp_t; Args are forwarded to any applicable constructor
template<typename T, typename Construct = construct_auto_t, typename... Args>
ref_ptr<T>
make_ref(Args &&...args)
{
  using tag_t = typename std::conditional<
      std::is_same<Construct, construct_auto_t>::value,
      typename std::conditional<
          std::is_constructible<T, ObjectClass::InitData>::value, construct_c_t,
          construct_cpp_t>::type,
      Construct>::type;
  return make_ref_tagged<T>(tag_t(), std::forward<Args>(args)...);
}

} // namespace detail

// TODO impl namespace ??

using detail::property;
using detail::property_read;
using detail::property_write;
using detail::signal;

using detail::make_ref;
using detail::ref_ptr;
using detail::ref_ptr_cast;
using detail::register_type;

namespace repository
{
namespace GObject
{
namespace impl
{
// bring into namespaces as in code generation

namespace internal
{
using ObjectClass = detail::ObjectClass;

} // namespace internal

using ObjectImpl = detail::ObjectImpl<Object, detail::ObjectClass>;

} // namespace impl

} // namespace GObject

} // namespace repository

} // namespace gi

#endif // GI_OBJECTCLASS_HPP
