#ifndef __GI_CPP_EXAMPLE_H__
#define __GI_CPP_EXAMPLE_H__

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

// enum
typedef enum _CEnum { ENUM_VALUE_0, ENUM_VALUE_1 } CEnum;
GType gi_cpp_enum_get_type(void);
#define GI_CPP_TYPE_ENUM (gi_cpp_enum_get_type())

// flags
typedef enum _CFlags { FLAG_VALUE_0 = 1, FLAG_VALUE_1 = 2 } CFlags;
GType gi_cpp_flags_get_type(void);
#define GI_CPP_TYPE_FLAGS (gi_cpp_flags_get_type())

// object
GType gi_cpp_example_get_type();

#define GI_CPP_TYPE_EXAMPLE (gi_cpp_example_get_type())
#define GI_CPP_EXAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GI_CPP_TYPE_EXAMPLE, GICppExample))
#define GI_CPP_EXAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GI_CPP_TYPE_EXAMPLE, GICppExampleClass))
#define GI_IS_CPP_EXAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GI_CPP_TYPE_EXAMPLE))
#define GI_IS_CPP_EXAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GI_CPP_TYPE_EXAMPLE))

typedef struct _GICppExample GICppExample;
typedef struct _GICppExampleClass GICppExampleClass;

struct _GICppExampleClass
{
  GObjectClass parent_class;

  /* virtual method */
  int (*vmethod)(GICppExample *, int a, int b);
  int (*cmethod)(GICppExample *, int a, int b);
};

enum {
  PROP_0,
  PROP_DATA,
  PROP_NUMBER,
  PROP_FNUMBER,
  PROP_OBJECT,
  PROP_PRESENT,
  PROP_ENUM,
  PROP_FLAGS,
  PROP_ERROR,
  PROP_LAST = PROP_ERROR
};

#define NAME_NUMBER "number"
#define NAME_FNUMBER "fnumber"
#define NAME_DATA "data"
#define NAME_PRESENT "present"
#define NAME_OBJECT "object"
#define NAME_ENUM "choice"
#define NAME_FLAGS "flags"
#define NAME_ERROR "error"

#define NAME_INUMBER "itf-number"

// interface

GICppExample *gi_cpp_example_new();

typedef struct _GICppExampleItf GICppExampleItf;
typedef struct _GICppExampleInterface GICppExampleInterface;

struct _GICppExampleInterface
{
  GTypeInterface iface;

  /* virtual method */
  int (*vmethod)(GICppExampleItf *, int a);
  int (*imethod)(GICppExampleItf *, int a);
};

GType gi_cpp_example_interface_get_type();

// property interface

typedef struct _GICppPropertyItf GICppPropertyItf;
typedef struct _GICppPropertyInterface GICppPropertyInterface;

struct _GICppPropertyInterface
{
  GTypeInterface iface;
};

GType gi_cpp_property_interface_get_type();

G_END_DECLS

#endif /* __GI_CPP_EXAMPLE_H__ */
