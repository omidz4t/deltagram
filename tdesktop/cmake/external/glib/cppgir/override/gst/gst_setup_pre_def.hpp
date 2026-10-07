#ifndef _GI_GST_SETUP_PRE_DEF_HPP_
#define _GI_GST_SETUP_PRE_DEF_HPP_

// some static inline are marked skipped, but make it through wrapping
// however, that leads to a reference to TU-local from non-TU-local in module
#ifdef GI_MODULE_IN_INTERFACE
// slightly less efficient, but it is the module way ...
#define GST_DISABLE_MINIOBJECT_INLINE_FUNCTIONS 1
#endif

#endif // _GI_GST_SETUP_PRE_DEF_HPP_
