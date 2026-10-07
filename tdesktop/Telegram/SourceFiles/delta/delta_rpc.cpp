#include "delta_rpc.h"

#include <QJsonDocument>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <sys/prctl.h>

using namespace Qt::StringLiterals;

DeltaRpc::DeltaRpc(QObject *parent)
: QObject(parent)
, _process(this) {
	connect(&_process, &QProcess::readyReadStandardOutput, this, [this] {
		onReadyRead();
	});
	_process.setChildProcessModifier([] {
		prctl(PR_SET_PDEATHSIG, SIGKILL);
	});
	connect(&_process, &QProcess::readyReadStandardError, this, [this] {
		const auto text = _process.readAllStandardError();
		if (!text.isEmpty()) {
			_stderrTail = (_stderrTail + text).right(4000);
			fprintf(stderr, "[deltachat-rpc-server] %s", text.constData());
		}
	});
	connect(&_process, &QProcess::finished, this, [this](
			int code,
			QProcess::ExitStatus status) {
		onFinished(code, status);
	});
	connect(&_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		if (_stopping) {
			return;
		}
		const auto err = _process.readAllStandardError();
		auto reason = _process.errorString();
		if (!err.isEmpty()) {
			reason += u"\n"_s + QString::fromUtf8(err);
		}
		failPending(reason);
		if (_onEvent) {
			QJsonObject error{{u"message"_s, reason}};
			_onEvent(QJsonObject{{u"error"_s, error}});
		}
	});
}

DeltaRpc::~DeltaRpc() {
	stop();
}

void DeltaRpc::start(const QString &program, const QString &accountsPath) {
	stop();
	_stopping = false;
	const auto loader = qEnvironmentVariable("DELTA_RPC_LDSO");
	const auto libraryPath = qEnvironmentVariable("DELTA_RPC_LIB");
	if (!loader.isEmpty()) {
		_process.setProgram(loader);
		_process.setArguments({
			u"--library-path"_s,
			libraryPath,
			program,
		});
	} else {
		_process.setProgram(program);
		_process.setArguments({});
	}
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.remove(u"LD_LIBRARY_PATH"_s);
	env.insert(u"DC_ACCOUNTS_PATH"_s, accountsPath);
	_process.setProcessEnvironment(env);
	_process.setProcessChannelMode(QProcess::SeparateChannels);
	_process.start();

	_startup = new QTimer(this);
	_startup->setSingleShot(true);
	connect(_startup, &QTimer::timeout, this, [this] {
		if (_process.state() == QProcess::NotRunning) {
			failPending(u"rpc server exited during startup"_s);
		}
	});
	_startup->start(15000);
	connect(&_process, &QProcess::started, this, [this] {
		if (_startup) {
			_startup->stop();
		}
	});
}

void DeltaRpc::stop() {
	_stopping = true;
	failPending(u"rpc stopped"_s);
	if (_process.state() == QProcess::NotRunning) {
		return;
	}
	_process.terminate();
	if (!_process.waitForFinished(2000)) {
		_process.kill();
		_process.waitForFinished(1000);
	}
}

bool DeltaRpc::running() const {
	return _process.state() != QProcess::NotRunning;
}

void DeltaRpc::setEventHandler(Callback handler) {
	_onEvent = std::move(handler);
}

void DeltaRpc::call(
		const QString &method,
		const QJsonArray &params,
		Callback done) {
	if (_process.state() == QProcess::NotRunning) {
		QJsonObject error{{u"message"_s, u"rpc server is not running"_s}};
		if (done) {
			done(QJsonObject{{u"error"_s, error}});
		}
		return;
	}
	const auto id = _nextId++;
	_pending.insert(id, std::move(done));
	QJsonObject request{
		{u"jsonrpc"_s, u"2.0"_s},
		{u"id"_s, static_cast<qint64>(id)},
		{u"method"_s, method},
		{u"params"_s, params},
	};
	const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
	_process.write(bytes);
	_process.write("\n");
}

void DeltaRpc::onReadyRead() {
	_buffer.append(_process.readAllStandardOutput());
	while (true) {
		const auto cut = _buffer.indexOf('\n');
		if (cut < 0) {
			break;
		}
		const auto line = _buffer.left(cut);
		_buffer.remove(0, cut + 1);
		if (!line.trimmed().isEmpty()) {
			handleLine(line);
		}
	}
}

void DeltaRpc::handleLine(const QByteArray &line) {
	QJsonParseError parseError;
	const auto doc = QJsonDocument::fromJson(line, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		QJsonObject error{{u"message"_s, u"malformed rpc line"_s}};
		error.insert(u"detail"_s, QString::fromUtf8(line));
		if (_onEvent) {
			_onEvent(QJsonObject{{u"error"_s, error}});
		}
		return;
	}
	const auto obj = doc.object();
	if (!obj.contains(u"id"_s)) {
		if (_onEvent) {
			_onEvent(obj);
		}
		return;
	}
	const auto id = static_cast<quint64>(obj.value(u"id"_s).toInteger());
	const auto done = _pending.take(id);
	if (done) {
		done(obj);
	}
}

void DeltaRpc::onFinished(int exitCode, QProcess::ExitStatus status) {
	auto reason = (status == QProcess::CrashExit)
		? u"rpc server crashed"_s
		: u"rpc server exited (%1)"_s.arg(exitCode);
	const auto stderrText = QString::fromUtf8(_stderrTail).trimmed();
	if (!stderrText.isEmpty()) {
		reason += u": "_s + stderrText.right(300);
	}
	failPending(reason);
	if (_stopping) {
		return;
	}
	if (_onEvent) {
		QJsonObject error{{u"message"_s, reason}};
		_onEvent(QJsonObject{{u"error"_s, error}});
	}
}

void DeltaRpc::failPending(const QString &reason) {
	const auto pending = std::exchange(_pending, {});
	QJsonObject error{{u"message"_s, reason}};
	const auto envelope = QJsonObject{{u"error"_s, error}};
	for (const auto &done : pending) {
		if (done) {
			done(envelope);
		}
	}
}
