#ifndef GI_HPP
#define GI_HPP

#include "base.hpp"
#include "container.hpp"
#include "enumflag.hpp"
#include "exception.hpp"
#include "expected.hpp"
#include "object.hpp"
#include "objectclass.hpp"
#include "string.hpp"

// check that include path has been setup properly to include override
#if defined(__has_include)
#if !__has_include(<glib/glib_extra_def.hpp>)
#warning "overrides not found in include path"
#endif
#endif

#endif // GI_HPP
