/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/launcher.h"

#include <QFile>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSurfaceFormat>
#include <QWindow>

#include <cstdlib>

extern "C" {
#include <openssl/crypto.h>
} // extern "C"

// Defined here so settings can ask without a new header. A failed probe
// aborts only this child. The running app stays on the raster backend.
bool DeltaTelGlProbeAllowsEnable() {
	const auto previous = qgetenv("DELTA_TEL_GL_PROBE");
	if (previous == "1") {
		return false;
	}
	QFile argsFile(QStringLiteral("/proc/self/cmdline"));
	if (!argsFile.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto raw = argsFile.readAll();
	QStringList args;
	QString current;
	for (const auto ch : raw) {
		if (ch == '\0') {
			if (!current.isEmpty()) {
				args.push_back(current);
			}
			current.clear();
		} else {
			current.push_back(QChar(ch));
		}
	}
	if (args.isEmpty()) {
		return false;
	}
	const auto program = args.takeFirst();
	auto env = QProcessEnvironment::systemEnvironment();
	env.remove(QStringLiteral("LD_LIBRARY_PATH"));
	env.insert(QStringLiteral("DELTA_TEL_GL_PROBE"), QStringLiteral("1"));
	QProcess process;
	process.setProcessEnvironment(env);
	process.start(program, args);
	if (!process.waitForStarted(3000) || !process.waitForFinished(8000)) {
		process.kill();
		process.waitForFinished(1000);
		return false;
	}
	return process.exitStatus() == QProcess::NormalExit
		&& process.exitCode() == 0;
}

static void deltaTelGlProbeOrContinue() {
	if (qgetenv("DELTA_TEL_GL_PROBE") != "1") {
		return;
	}
	int argc = 1;
	char name[] = "delta-tel-gl-probe";
	char *argv[] = { name, nullptr };
	QSurfaceFormat format;
	format.setVersion(4, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setRenderableType(QSurfaceFormat::OpenGL);
	QSurfaceFormat::setDefaultFormat(format);
	QGuiApplication app(argc, argv);
	QWindow window;
	window.setSurfaceType(QSurface::OpenGLSurface);
	window.setFormat(format);
	window.create();
	QOpenGLContext context;
	context.setFormat(format);
	const auto ok = context.create() && context.makeCurrent(&window);
	std::_Exit(ok ? 0 : 1);
}

int main(int argc, char *argv[]) {
	deltaTelGlProbeOrContinue();
	// OpenSSL's own atexit handler runs OPENSSL_cleanup() on the main thread
	// and frees the library globals while detached background tasks can still
	// be inside OpenSSL, so an ordinary quit can fault on a worker thread.
	// Suppressing that registration leaves the library state alive until the
	// process exits, which is the trade we want: the OS reclaims it anyway.
	OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr);

	const auto launcher = Core::Launcher::Create(argc, argv);
	return launcher ? launcher->exec() : 1;
}
