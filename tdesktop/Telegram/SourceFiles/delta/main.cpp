#include "delta_rpc.h"
#include "shell.h"

#include <cstdio>

#include <QApplication>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

QString errorText(const QJsonObject &reply) {
	const auto error = reply.value(u"error"_s).toObject();
	const auto message = error.value(u"message"_s).toString();
	return message.isEmpty()
		? QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact))
		: message;
}

void finish(int code, const QString &text) {
	fprintf(stderr, "%s\n", qUtf8Printable(text));
	QCoreApplication::exit(code);
}

} // namespace

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	QApplication::setApplicationName(u"delta-shell"_s);

	QCommandLineParser parser;
	parser.addHelpOption();
	QCommandLineOption rpcOption(u"rpc"_s, u"Path to deltachat-rpc-server"_s, u"path"_s);
	QCommandLineOption accountsOption(
		u"accounts"_s,
		u"DC_ACCOUNTS_PATH"_s,
		u"path"_s,
		u"accounts"_s);
	QCommandLineOption checkOption(u"check"_s, u"Call get_system_info and exit"_s);
	QCommandLineOption selfTestOption(
		u"self-test"_s,
		u"Create an account, list chats, and exit"_s);
	parser.addOption(rpcOption);
	parser.addOption(accountsOption);
	parser.addOption(checkOption);
	parser.addOption(selfTestOption);
	parser.process(app);

	const auto program = parser.value(rpcOption);
	if (program.isEmpty()) {
		fprintf(stderr, "Pass --rpc /path/to/deltachat-rpc-server\n");
		return 2;
	}

	auto *rpc = new DeltaRpc(&app);
	rpc->start(program, parser.value(accountsOption));

	if (parser.isSet(checkOption)) {
		QTimer::singleShot(400, &app, [rpc] {
			rpc->call(u"get_system_info"_s, {}, [](QJsonObject reply) {
				if (reply.contains(u"error"_s)) {
					finish(1, errorText(reply));
					return;
				}
				finish(0, QString::fromUtf8(QJsonDocument(reply.value(u"result"_s).toObject())
					.toJson(QJsonDocument::Indented)));
			});
		});
		return QApplication::exec();
	}

	if (parser.isSet(selfTestOption)) {
		QTimer::singleShot(400, &app, [rpc] {
			rpc->call(u"add_account"_s, {}, [rpc](QJsonObject created) {
				if (created.contains(u"error"_s)) {
					finish(1, errorText(created));
					return;
				}
				const auto accountId = created.value(u"result"_s).toInt();
				rpc->call(u"is_configured"_s, {accountId}, [rpc, accountId](QJsonObject configured) {
					if (configured.contains(u"error"_s) || configured.value(u"result"_s).toBool()) {
						finish(1, u"new account should be unconfigured"_s);
						return;
					}
					rpc->call(
						u"get_chatlist_entries"_s,
						{accountId, QJsonValue::Null, QJsonValue::Null, QJsonValue::Null},
						[accountId](QJsonObject entries) {
							if (entries.contains(u"error"_s)) {
								finish(1, errorText(entries));
								return;
							}
							const auto count = entries.value(u"result"_s).toArray().size();
							finish(0, u"account %1 chats %2"_s.arg(accountId).arg(count));
						});
				});
			});
		});
		return QApplication::exec();
	}

	auto *shell = new Shell(rpc);
	shell->show();
	QTimer::singleShot(400, shell, [shell] { shell->bootstrap(); });
	return QApplication::exec();
}
