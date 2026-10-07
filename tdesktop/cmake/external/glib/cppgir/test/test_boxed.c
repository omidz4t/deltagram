#include "test_boxed.h"

#include <glib.h>
#include <glib-object.h>

GBExample* gi_cpp_gbexample_new ()
{
  return g_new0 (GBExample, 1);
}

static GBExample*
gb_example_copy (const GBExample * src)
{
  GBExample *out = gi_cpp_gbexample_new ();
  *out = *src;
  return out;
}

static void
gb_example_free (GBExample * ex)
{
  g_free (ex);
}

G_DEFINE_BOXED_TYPE (GBExample, gi_cpp_gbexample, gb_example_copy, gb_example_free)

CBExample* gi_cpp_cbexample_new ()
{
  return g_new0 (CBExample, 1);
}

void gi_cpp_cbexample_free (CBExample * ex)
{
  g_free (ex);
}

