#include "test_object.h"

#include <glib.h>
#include <glib-object.h>

// helper enum
GType
gi_cpp_enum_get_type (void)
{
  static GType enum_type = 0;

  static const GEnumValue enum_types[] = {
    {ENUM_VALUE_0, "EnumValue0", "v0"},
    {ENUM_VALUE_1, "EnumValue1", "v1"},
    {0, NULL, NULL}
  };

  if (!enum_type) {
    enum_type = g_enum_register_static ("GICppEnum", enum_types);
  }
  return enum_type;
}

// helper flags
GType
gi_cpp_flags_get_type (void)
{
  static GType flags_type = 0;

  static const GFlagsValue flags_types[] = {
    {FLAG_VALUE_0, "FlagValue0", "f0"},
    {FLAG_VALUE_1, "FlagValue1", "f1"},
    {0, NULL, NULL}
  };

  if (!flags_type) {
    flags_type = g_flags_register_static ("GICppFlags", flags_types);
  }
  return flags_type;
}

// interface
typedef GICppExampleInterface GICppIExampleInterface;
G_DEFINE_INTERFACE (GICppIExample, gi_cpp_example_interface, 0)

static void
gi_cpp_example_interface_default_init (GICppExampleInterface *iface)
{
  (void) iface;
}

// property interface
typedef GICppPropertyInterface GICppIPropertyInterface;
G_DEFINE_INTERFACE (GICppIProperty, gi_cpp_property_interface, 0)

static void
gi_cpp_property_interface_default_init (GICppPropertyInterface *iface)
{
  (void) iface;

  g_object_interface_install_property (iface,
      g_param_spec_int (NAME_INUMBER, "ItfNumber",
          "ItfNumber", 0, 50, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

enum {
  EXAMPLE_TO_INT,
  EXAMPLE_TO_STRING,
  EXAMPLE_TO_VOID,
  EXAMPLE_TO_OUTPUT_INT,
  /* FILL ME */
  LAST_SIGNAL
};

static guint example_signals[LAST_SIGNAL] = { 0 };

struct _GICppExample
{
  GObject object;

  /* properties */
  gchar *data;
  gint number;
  gdouble fnumber;
  GObject *obj;
  gboolean present;
  CEnum enumvalue;
  CFlags flagsvalue;
  GError *error;
  gint itf_number;
};

static int
vmethod_interface_default (GICppExampleItf * itf, int a)
{
  // sanity check
  g_assert (GI_IS_CPP_EXAMPLE (itf));

  return 2 + a;
}

static void
gi_cpp_example_interface_init (GICppExampleInterface *iface)
{
  iface->vmethod = vmethod_interface_default;
}

#define gi_cpp_example_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GICppExample, gi_cpp_example, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (gi_cpp_example_interface_get_type(), gi_cpp_example_interface_init))

static void
gi_cpp_example_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GICppExample *ex = GI_CPP_EXAMPLE (object);

  switch (prop_id) {
    case PROP_DATA:
      g_value_set_string (value, ex->data);
      break;
    case PROP_NUMBER:
      g_value_set_int (value, ex->number);
      break;
    case PROP_FNUMBER:
      g_value_set_double (value, ex->fnumber);
      break;
    case PROP_PRESENT:
      g_value_set_boolean (value, ex->present);
      break;
    case PROP_OBJECT:
      g_value_set_object (value, ex->obj);
      break;
    case PROP_ENUM:
      g_value_set_enum (value, ex->enumvalue);
      break;
    case PROP_FLAGS:
      g_value_set_flags (value, ex->flagsvalue);
      break;
    case PROP_ERROR:
      g_value_set_boxed (value, ex->error);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gi_cpp_example_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GICppExample *ex = GI_CPP_EXAMPLE (object);

  switch (prop_id) {
    case PROP_DATA:
      g_free (ex->data);
      ex->data = g_value_dup_string (value);
      break;
    case PROP_NUMBER:
      ex->number = g_value_get_int (value);
      break;
    case PROP_FNUMBER:
      ex->fnumber = g_value_get_double (value);
      break;
    case PROP_PRESENT:
      ex->present = g_value_get_boolean (value);
      break;
    case PROP_OBJECT:
      if (ex->obj)
        g_object_unref (ex->obj);
      ex->obj = g_value_dup_object (value);
      break;
    case PROP_ENUM:
      ex->enumvalue = g_value_get_enum (value);
      break;
    case PROP_FLAGS:
      ex->flagsvalue = g_value_get_flags (value);
      break;
    case PROP_ERROR:
      if (ex->error)
        g_error_free(ex->error);
      ex->error = g_value_dup_boxed(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gi_cpp_example_finalize (GObject * object)
{
  GICppExample *ex = GI_CPP_EXAMPLE (object);

  g_free (ex->data);
  if (ex->obj)
    g_object_unref (ex->obj);
  if (ex->error)
    g_error_free(ex->error);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static int
vmethod_default (GICppExample * ex, int a, int b)
{
  // sanity check
  g_assert (GI_IS_CPP_EXAMPLE (ex));

  (void)ex;
  return a + b;
}

static void
gi_cpp_example_class_init (GICppExampleClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gi_cpp_example_finalize;
  gobject_class->set_property = gi_cpp_example_set_property;
  gobject_class->get_property = gi_cpp_example_get_property;

  g_object_class_install_property (gobject_class, PROP_NUMBER,
      g_param_spec_int (NAME_NUMBER, "Number",
          "Number", 0, 50, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FNUMBER,
      g_param_spec_double (NAME_FNUMBER, "FNumber",
          "FNumber", 0, 50, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DATA,
      g_param_spec_string (NAME_DATA, "Data", "Data", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PRESENT,
      g_param_spec_boolean (NAME_PRESENT, "Present", "Present",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OBJECT,
      g_param_spec_object (NAME_OBJECT, "Object", "Object",
          G_TYPE_OBJECT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENUM,
      g_param_spec_enum (NAME_ENUM, "Enum", "Enum",
          GI_CPP_TYPE_ENUM, ENUM_VALUE_0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FLAGS,
      g_param_spec_flags (NAME_FLAGS, "Flags", "Flags",
          GI_CPP_TYPE_FLAGS, FLAG_VALUE_0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ERROR,
      g_param_spec_boxed (NAME_ERROR, "Error", "Error",
          G_TYPE_ERROR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  example_signals[EXAMPLE_TO_INT] =
      g_signal_new ("to-int", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_INT, 4,
      G_TYPE_OBJECT, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING);

  example_signals[EXAMPLE_TO_STRING] =
      g_signal_new ("to-string", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_STRING, 2,
      G_TYPE_INT, G_TYPE_INT64);

  example_signals[EXAMPLE_TO_VOID] =
      g_signal_new ("to-void", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 3,
      G_TYPE_DOUBLE, GI_CPP_TYPE_ENUM, GI_CPP_TYPE_FLAGS);

  example_signals[EXAMPLE_TO_VOID] =
      g_signal_new ("to-output-int", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 3, G_TYPE_POINTER, G_TYPE_PTR_ARRAY, G_TYPE_POINTER);

  klass->vmethod = vmethod_default;
}

static void
gi_cpp_example_init (GICppExample * ex)
{
  (void)ex;
}

GICppExample *
gi_cpp_example_new ()
{
  return g_object_new (GI_CPP_TYPE_EXAMPLE, NULL);
}
