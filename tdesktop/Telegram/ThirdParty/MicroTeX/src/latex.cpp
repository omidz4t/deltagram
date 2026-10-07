#include "latex.h"

#include "atom/atom_basic.h"
#include "atom/atom_impl.h"
#include "atom/atom_matrix.h"
#include "atom/atom_row.h"
#include "core/core.h"
#include "core/formula.h"
#include "core/macro.h"
#include "core/parser.h"
#include "fonts/fonts.h"
#if CLATEX_CXX17 && !defined(__APPLE__)
#include <filesystem>
#endif

using namespace std;
using namespace tex;

string tex::RES_BASE = "res";
static string CHECK_FILE = ".clatexmath-res_root";
#ifdef _WIN32
static char PATH_SEPERATOR = ';';
#else
static char PATH_SEPERATOR = ':';
#endif

Formula* LaTeX::_formula = nullptr;
TeXRenderBuilder* LaTeX::_builder = nullptr;

string LaTeX::queryResourceLocation(string& custom_path) {
  queue<string> paths;
  paths.push(custom_path);

  // checks if CLM_DEVEL exists. If it does, it pushes it to potential paths.
  char* devel = getenv("CLM_DEVEL");
  if (devel != NULL && *devel != '\0') {
    paths.push(string(devel));
  }

  // checks if XDG_DATA_HOME exists. If it does, it pushes it to potential paths.
  char* userdata = getenv("XDG_DATA_HOME");
  if (userdata != NULL && strcmp(userdata, "") != 0) {
    paths.push(string(userdata) + "/clatexmath/");
  }

  // checks if XDG_DATA_DIRS is set. If it is, it splits XDG_DATA_DIRS at : (or
  // ; on windows) and pushes each part of it to potential paths.
  char* xdg = getenv("XDG_DATA_DIRS");
  if (xdg != NULL && strcmp(xdg, "") != 0) {
    stringstream xdg_paths(xdg);
    string xdg_path;
    while (getline(xdg_paths, xdg_path, PATH_SEPERATOR)) {
      paths.push(xdg_path + "/clatexmath/");
    }
  }
#ifndef _WIN32
  // checks if HOME env var is unset. if it isn't it pushes ~/.local/share/clatexmath
  // to potential paths.
  char* home = getenv("HOME");
  if (home != NULL && strcmp(home, "") != 0) {
    char* userdata_fallback = nullptr;
    // asprintf returns malloc'd memory (delete would be a mismatched-free)
    // and leaves the pointer undefined on failure.
    if (asprintf(&userdata_fallback, "%s/.local/share/clatexmath/", home) >= 0) {
      paths.push(string(userdata_fallback));
      free(userdata_fallback);
    }
  }
  paths.push("/usr/share/clatexmath/");
  paths.push("/usr/local/share/clatexmath/");
#endif
  // goes through the list of potential paths. if it finds a path that contains
  // .clatexmath-res_root, it returns it. Otherwise return an empty string.
  while (!paths.empty()) {
#if CLATEX_CXX17 && !defined(__APPLE__)
    filesystem::path p = paths.front();
    p.append(CHECK_FILE);
    if (filesystem::exists(p)) {
      p.remove_filename();
      return p.string();
    }
#elif defined(_MSC_VER)
    std::string path = paths.front();
    std::string file = path + "/" + CHECK_FILE;
    FILE* fp = NULL;
    errno_t err = fopen_s(&fp, file.c_str(), "rb");
    if (err == 0 && fp) {
      fclose(fp);
      return path;
    }
#else
    std::string path = paths.front();
    std::string file = path + "/" + CHECK_FILE;
    FILE* fp = fopen(file.c_str(), "rb");
    if (fp) {
      fclose(fp);
      return path;
    }
#endif
    paths.pop();
  }
  return "";
}

void LaTeX::init(string res_root_path) {
  try {
    auto path = queryResourceLocation(res_root_path);
    if (!path.empty()) {
      RES_BASE = path;
    }
  } catch (std::exception&) {
  }
  if (_formula != nullptr) return;

  NewCommandMacro::_init_();
  DefaultTeXFont::_init_();
  Formula::_init_();
  TextRenderingBox::_init_();

  // Snapshot the built-in macro set now that all predefined commands and
  // environments are registered, so _reset() (run before each parse) can
  // roll back anything a formula defines without dropping the built-ins.
  NewCommandMacro::_captureBuiltins();

  _formula = new Formula();
  _builder = new TeXRenderBuilder();
}

void LaTeX::initBundled() {
  // PROBE PATCH: bypass queryResourceLocation entirely. RES_BASE keeps its
  // default "res", and Font_qt(file, size) in platform/qt/graphic_qt.cpp
  // prepends ":/" when the file is missing on disk -- so font references
  // resolve to ":/res/fonts/..." and can be served from a bundled .qrc.
  if (_formula != nullptr) return;

  NewCommandMacro::_init_();
  DefaultTeXFont::_init_();
  Formula::_init_();
  TextRenderingBox::_init_();

  // Snapshot the built-in macro set now that all predefined commands and
  // environments are registered, so _reset() (run before each parse) can
  // roll back anything a formula defines without dropping the built-ins.
  NewCommandMacro::_captureBuiltins();

  _formula = new Formula();
  _builder = new TeXRenderBuilder();
}

void LaTeX::release() {
  DefaultTeXFont::_free_();
  Formula::_free_();
  MacroInfo::_free_();
  NewCommandMacro::_free_();
  TextRenderingBox::_free_();

  if (_formula != nullptr) delete _formula;
  if (_builder != nullptr) delete _builder;
}

const string& LaTeX::getResRootPath() {
  return RES_BASE;
}

void LaTeX::setDebug(bool debug) {
  Formula::setDEBUG(debug);
}

TeXRender* LaTeX::parse(const wstring& latex, int width, float textSize, float lineSpace, color fg) {
  // Untrusted input: roll back any global state a previous formula mutated
  // (user macros, \newcolumntype column types, \arrayrulecolor line color,
  // \definecolor palette entries, \cornersize oval radius, \externalfont text
  // font, \breakEverywhere, \DeclareMathSizes / \magnification sizes) so
  // definitions can't leak between formulas. A formula's own definitions
  // still apply within itself (they are made during this parse, after this
  // reset).
  NewCommandMacro::_reset();
  MatrixAtom::resetState();
  ColorAtom::resetColors();
  OvalAtom::resetState();
  TextRenderingBox::resetFont();
  RowAtom::_breakEveywhere = false;
  DefaultTeXFont::resetMathSizes();
  resetBoxBudget();
  resetExpansionWork();

  bool lined = true;
  if (startswith(latex, L"$$") || startswith(latex, L"\\[")) {
    lined = false;
  }
  Alignment align = lined ? Alignment::left : Alignment::center;
  _formula->setLaTeX(latex);
  TeXRender* render =
    _builder->setStyle(TexStyle::display)
      .setTextSize(textSize)
      .setWidth(UnitType::pixel, width, align)
      .setIsMaxWidth(lined)
      .setLineSpace(UnitType::pixel, lineSpace)
      .setForeground(fg)
      .build(*_formula);
  return render;
}
