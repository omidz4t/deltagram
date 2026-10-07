#define GI_INLINE 1
#include <gio/gio.hpp>

#include <functional>
#include <iostream>
#include <memory>

#include <QCoreApplication>

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>

namespace GLib = gi::repository::GLib;
namespace GObject_ = gi::repository::GObject;
namespace Gio = gi::repository::Gio;

template<typename T>
using ResultExtractor = std::function<void(
    GObject_::Object, Gio::AsyncResult result, QFutureInterface<T> &fut)>;

template<typename T>
class qt_future
{
  struct CallbackData
  {
    QFutureInterface<T> promise;
    QFutureWatcher<T> watcher;
    Gio::Cancellable cancel;
    ResultExtractor<T> handler;
  };

  std::shared_ptr<CallbackData> data_;

public:
  qt_future(ResultExtractor<T> h)
  {
    data_ = std::make_shared<CallbackData>();
    data_->handler = h;
  }

  QFuture<T> future() { return data_->promise.future(); }

  QFutureInterface<T> promise() { return data_->promise; }

  operator Gio::AsyncReadyCallback()
  {
    auto d = data_;
    return [d](GObject_::Object obj, Gio::AsyncResult result) {
      assert(d->handler);
      d->handler(obj, result, d->promise);
    };
  }

  Gio::Cancellable cancellable()
  {
    if (!data_->cancel) {
      auto cancel = data_->cancel = Gio::Cancellable::new_();
      data_->watcher.setFuture(future());
      auto h = [cancel]() mutable { cancel.cancel(); };
      data_->watcher.connect(
          &data_->watcher, &decltype(data_->watcher)::finished, std::move(h));
    }
    return data_->cancel;
  }

  // why not ... ??
  operator Gio::Cancellable() { return cancellable(); }
};

#if GI_CONST_METHOD
#define CONST_METHOD const
#else
#define CONST_METHOD
#endif

template<typename Result, typename Object>
qt_future<Result>
make_future(Result (Object::*mf)(Gio::AsyncResult, GLib::Error *) CONST_METHOD)
{
  ResultExtractor<Result> f;
  f = [mf](GObject_::Object cbobj, Gio::AsyncResult result,
          QFutureInterface<Result> &fut) mutable {
    auto obj = gi::object_cast<Object>(cbobj);
    GLib::Error error{};
    auto res = (obj.*mf)(result, &error);
    if (error.gobj_()) {
      // FIXME use std::expected<Result> as a result type ??
      // reportException might be an option, if enabled, but leads to throw
      fut.reportFinished();
      qWarning() << "error code " << error.code_();
      qWarning() << "error message "
                 << QString::fromUtf8(error.message_().c_str());
    } else {
      fut.reportFinished(&res);
    }
  };
  return {f};
}

// ====
// some small future-centric API wrappers using above helper class

QFuture<Gio::FileInfo>
file_query_file_system_info(
    Gio::File f, const std::string &attributes, gint io_priority)
{
  auto fut = make_future(&Gio::File::query_filesystem_info_finish);
  f.query_filesystem_info_async(attributes, io_priority, fut, fut);
  return fut.future();
}

QFuture<bool>
file_copy(Gio::File src, Gio::File destination, Gio::FileCopyFlags flags,
    gint io_priority)
{
  auto fut = make_future(&Gio::File::copy_finish);
  auto promise = fut.promise();
  auto p = [promise](
               goffset current_num_bytes, goffset total_num_bytes) mutable {
    promise.setProgressRange(0, total_num_bytes);
    promise.setProgressValue(current_num_bytes);
  };
  src.copy_async(destination, flags, io_priority, fut, p, fut);
  return fut.future();
}

// ====

// save on typing elsewhere
// also makes it movable in a way
template<typename T>
std::shared_ptr<QFutureWatcher<T>>
make_watcher(QFuture<T> fut)
{
  auto watcher = std::make_shared<QFutureWatcher<T>>();
  watcher->setFuture(fut);
  return watcher;
}

// type-erased owner/cleanup type
using Retainer = std::shared_ptr<std::nullptr_t>;

Retainer
do_query(QCoreApplication &app, const std::string &fpath)
{
  auto file = Gio::File::new_for_commandline_arg(fpath);
  auto fut = file_query_file_system_info(file, "*", GLib::PRIORITY_DEFAULT_);
  auto watcher = make_watcher(fut);
  auto h = [fut, &app]() {
    Q_ASSERT(fut.isFinished());
    std::cout << "query finished" << std::endl;
    if (fut.resultCount()) {
      auto finfo = fut.result();
      for (auto attr : {Gio::FILE_ATTRIBUTE_FILESYSTEM_TYPE_,
               Gio::FILE_ATTRIBUTE_FILESYSTEM_FREE_,
               Gio::FILE_ATTRIBUTE_FILESYSTEM_SIZE_}) {
        std::cout << attr << ": " << finfo.get_attribute_as_string(attr)
                  << std::endl;
      }
    } else {
      std::cout << "... but no results" << std::endl;
    }
    app.quit();
  };
  watcher->connect(
      watcher.get(), &decltype(watcher)::element_type::finished, &app, h);
  return {watcher, nullptr};
}

Retainer
do_copy(QCoreApplication &app, const std::string &src, const std::string &dest)
{
  auto fsrc = Gio::File::new_for_commandline_arg(src);
  auto fdest = Gio::File::new_for_commandline_arg(dest);
  auto fut = file_copy(
      fsrc, fdest, Gio::FileCopyFlags::ALL_METADATA_, GLib::PRIORITY_DEFAULT_);
  auto watcher = make_watcher(fut);
  auto h = [fut, &app]() {
    Q_ASSERT(fut.isFinished());
    std::cout << "copy finished" << std::endl;
    if (fut.resultCount()) {
      auto ok = fut.result();
      if (ok) {
        std::cout << "copy ok" << std::endl;
      } else {
        std::cout << "copy failed" << std::endl;
      }
    } else {
      std::cout << "... but no results" << std::endl;
    }
    app.quit();
  };
  watcher->connect(
      watcher.get(), &decltype(watcher)::element_type::finished, &app, h);
  // also monitor progress
  auto progress = [fut](int) {
    auto max = fut.progressMaximum();
    auto min = fut.progressMinimum();
    auto current = fut.progressValue();
    std::cout << "copy progress " << current << " (" << min << " -> " << max
              << ")" << std::endl;
  };
  watcher->connect(watcher.get(),
      &decltype(watcher)::element_type::progressValueChanged, &app, progress);
  return {watcher, nullptr};
}

int
main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  if (argc < 2)
    return -1;

  Retainer r;
  if (argc == 2) {
    r = do_query(app, argv[1]);
  } else if (argc == 3) {
    r = do_copy(app, argv[1], argv[2]);
  }

  return app.exec();
}
