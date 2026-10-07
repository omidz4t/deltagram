#ifndef TEST_BOXED_H
#define TEST_BOXED_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

GType gi_cpp_gbexample_get_type();

#define GI_CPP_TYPE_BOXED_EXAMPLE (gi_cpp_gbexample_get_type())

/* G_TYPE_BOXED example */

typedef struct _GBExample
{
  int data;
} GBExample;

GBExample *gi_cpp_gbexample_new();

/* plain struct example */

typedef struct _CBExample
{
  int data;
} CBExample;

CBExample *gi_cpp_cbexample_new();
void gi_cpp_cbexample_free(CBExample *);

G_END_DECLS

#endif // TEST_BOXED_H
