/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_codes.h"

#include "ui/toast/toast.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "core/application.h"
#include "delta/account_backend.h"
#include "mtproto/web_proxy/web_proxy_transport.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/window_session_controller.h"
#include "media/audio/media_audio_track.h"
#include "settings/sections/settings_folders.h"
#include "storage/storage_account.h"
#include "api/api_updates.h"
#include "base/qt/qt_common_adapters.h"
#include "base/custom_app_icon.h"
#include "base/options.h"
#include "boxes/abstract_box.h" // Ui::show().

#include <zlib.h>

namespace Settings {
namespace {

using SessionController = Window::SessionController;

[[nodiscard]] QByteArray UnpackRawGzip(const QByteArray &bytes) {
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	stream.avail_in = 0;
	stream.next_in = nullptr;
	int res = inflateInit2(&stream, -MAX_WBITS);
	if (res != Z_OK) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(1024 * 1024, char(0));
	stream.avail_in = bytes.size();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
	stream.avail_out = 0;
	while (!stream.avail_out) {
		stream.avail_out = result.size();
		stream.next_out = reinterpret_cast<Bytef*>(result.data());
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			return QByteArray();
		} else if (!stream.avail_out) {
			return QByteArray();
		}
	}
	result.resize(result.size() - stream.avail_out);
	return result;
}

auto GenerateCodes() {
	auto codes = std::map<QString, Fn<void(SessionController*, Main::Account*)>>();
	codes.emplace(u"debugmode"_q, [](SessionController *window, Main::Account*) {
		QString text = Logs::DebugEnabled()
			? u"Do you want to disable DEBUG logs?"_q
			: u"Do you want to enable DEBUG logs?\n\nAll network events will be logged."_q;
		Ui::show(Ui::MakeConfirmBox({ text, [] {
			Core::App().switchDebugMode();
		} }));
	});
	codes.emplace(u"viewlogs"_q, [](SessionController *window, Main::Account*) {
		File::ShowInFolder(cWorkingDir() + "log.txt");
	});
	if (!Core::UpdaterDisabled()) {
		codes.emplace(u"testupdate"_q, [](SessionController *window, Main::Account*) {
			Core::UpdateChecker().test();
		});
	}
	codes.emplace(u"loadlang"_q, [](SessionController *window, Main::Account *account) {
		Lang::CurrentCloudManager().switchToLanguage({ u"#custom"_q }, account);
	});
	codes.emplace(u"crashplease"_q, [](SessionController *window, Main::Account*) {
		Unexpected("Crashed in Settings!");
	});
	codes.emplace(u"moderate"_q, [](SessionController *window, Main::Account*) {
		auto text = Core::App().settings().moderateModeEnabled() ? u"Disable moderate mode?"_q : u"Enable moderate mode?"_q;
		Ui::show(Ui::MakeConfirmBox({ text, [=] {
			Core::App().settings().setModerateModeEnabled(!Core::App().settings().moderateModeEnabled());
			Core::App().saveSettingsDelayed();
			Ui::hideLayer();
		} }));
	});
	codes.emplace(u"getdifference"_q, [](SessionController*, Main::Account *account) {
		if (!Delta::UsesTelegramBackend(account)) {
			return;
		}
		if (const auto session = account->maybeSession()) {
			session->updates().getDifference();
		}
	});
	codes.emplace(u"loadcolors"_q, [](SessionController *window, Main::Account*) {
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open palette file", "Palette (*.tdesktop-palette)", [](const FileDialog::OpenResult &result) {
			if (!result.paths.isEmpty()) {
				Window::Theme::Apply(result.paths.front());
			}
		});
	});
	codes.emplace(u"endpoints"_q, [](SessionController*, Main::Account *account) {
		if (!Delta::UsesTelegramBackend(account)) {
			return;
		}
		if (!Core::App().domain().started()) {
			return;
		}
		const auto weak = base::make_weak(account);
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open DC endpoints", "DC Endpoints (*.tdesktop-endpoints)", [weak](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty()) {
				return;
			}
			if (const auto strong = weak.get()) {
				if (!strong->mtp().dcOptions().loadFromFile(result.paths.front())) {
					Ui::show(Ui::MakeInformBox("Could not load endpoints"
						" :( Errors in 'log.txt'."));
				}
			}
		});
	});
	codes.emplace(u"testmode"_q, [](SessionController*, Main::Account *account) {
		if (!Delta::UsesTelegramBackend(account)) {
			return;
		}
		auto &domain = Core::App().domain();
		if (domain.started()
			&& (domain.accounts().size() == 1)
			&& !domain.active().sessionExists()) {
			const auto environment = domain.active().mtp().environment();
			domain.addActivated([&] {
				return (environment == MTP::Environment::Production)
					? MTP::Environment::Test
					: MTP::Environment::Production;
			}());
			Ui::Toast::Show((environment == MTP::Environment::Production)
				? "Switched to the test environment."
				: "Switched to the production environment.");
		}
	});
	codes.emplace(u"folders"_q, [](SessionController *window, Main::Account*) {
		if (window) {
			window->showSettings(Settings::FoldersId());
		}
	});
	codes.emplace(u"registertg"_q, [](SessionController *window, Main::Account*) {
		Core::Application::RegisterUrlScheme();
		Ui::Toast::Show("Forced custom scheme register.");
	});
	codes.emplace(u"numberbuttons"_q, [](SessionController *window, Main::Account*) {
		using namespace base::options;
		auto &option = lookup<bool>(kOptionFastButtonsMode);
		const auto now = !option.value();
		option.set(now);
		Ui::Toast::Show(now
			? u"Fast buttons mode enabled."_q
			: u"Fast buttons mode disabled."_q);
	});
	codes.emplace(u"externalweb"_q, [](SessionController *window, Main::Account*) {
		const auto disabled = MTP::WebProxy::Transport::ToggleWebviewDisabled();
		Ui::Toast::Show(disabled
			? u"WebView transport blocked."_q
			: u"WebView transport unblocked."_q);
	});

	auto audioFilters = u"Audio files (*.wav *.mp3);;"_q + FileDialog::AllFilesFilter();
	auto audioKeys = {
		u"msg_incoming"_q,
		u"call_incoming"_q,
		u"call_outgoing"_q,
		u"call_busy"_q,
		u"call_connect"_q,
		u"call_end"_q,
	};
	for (auto &key : audioKeys) {
		codes.emplace(key, [=](SessionController *window, Main::Account*) {
			FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open audio file", audioFilters, [=](const FileDialog::OpenResult &result) {
				if (!result.paths.isEmpty()) {
					auto track = Media::Audio::Current().createTrack();
					track->fillFromFile(result.paths.front());
					if (track->failed()) {
						Ui::show(Ui::MakeInformBox(
							"Could not audio :( Errors in 'log.txt'."));
					} else {
						Core::App().settings().setSoundOverride(
							key,
							result.paths.front());
						Core::App().saveSettingsDelayed();
					}
				}
			});
		});
	}
	codes.emplace(u"sounds_reset"_q, [](SessionController *window, Main::Account*) {
		Core::App().settings().clearSoundOverrides();
		Core::App().saveSettingsDelayed();
		Ui::show(Ui::MakeInformBox("All sound overrides were reset."));
	});
	codes.emplace(u"unpacklog"_q, [](SessionController *window, Main::Account*) {
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open crash log file", "Crash dump (*.txt)", [=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty()) {
				return;
			}
			auto f = QFile(result.paths.front());
			if (!f.open(QIODevice::ReadOnly)) {
				Ui::Toast::Show("Could not open log :(");
				return;
			}
			const auto all = f.readAll();
			const auto log = all.indexOf("Log: ");
			if (log < 0) {
				Ui::Toast::Show("Could not find log :(");
				return;
			}
			const auto base = all.mid(log + 5);
			const auto end = base.indexOf('\n');
			if (end <= 0) {
				Ui::Toast::Show("Could not find log end :(");
				return;
			}
			const auto based = QByteArray::fromBase64(base.mid(0, end));
			const auto uncompressed = UnpackRawGzip(based);
			if (uncompressed.isEmpty()) {
				Ui::Toast::Show("Could not unpack log :(");
				return;
			}
			FileDialog::GetWritePath(Core::App().getFileDialogParent(), "Save detailed log", "Crash dump (*.txt)", QString(), [=](QString &&result) {
				if (result.isEmpty()) {
					return;
				}
				auto f = QFile(result);
				if (!f.open(QIODevice::WriteOnly)) {
					Ui::Toast::Show("Could not open details :(");
				} else if (f.write(uncompressed) != uncompressed.size()) {
					Ui::Toast::Show("Could not write details :(");
				} else {
					f.close();
					Ui::Toast::Show("Done!");
				}
			});
		});
	});
	codes.emplace(u"testchatcolors"_q, [](SessionController *window, Main::Account*) {
		const auto now = !Data::CloudThemes::TestingColors();
		Data::CloudThemes::SetTestingColors(now);
		Ui::Toast::Show(now ? "Testing chat theme colors!" : "Not testing..");
	});

#ifdef Q_OS_MAC
	codes.emplace(u"customicon"_q, [](SessionController *window, Main::Account*) {
		const auto iconFilters = u"Icon files (*.icns *.png);;"_q + FileDialog::AllFilesFilter();
		const auto change = [](const QString &path) {
			const auto success = path.isEmpty()
				? base::ClearCustomAppIcon()
				: base::SetCustomAppIcon(path);
			Ui::Toast::Show(success
				? (path.isEmpty()
					? "Icon cleared. Restarting the Dock."
					: "Icon updated. Restarting the Dock.")
				: (path.isEmpty()
					? "Icon clear failed. See log.txt for details."
					: "Icon update failed. See log.txt for details."));
		};
		FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Choose custom icon", iconFilters, [=](const FileDialog::OpenResult &result) {
			change(result.paths.isEmpty() ? QString() : result.paths.front());
		}, [=] {
			change(QString());
		});
	});
#endif // Q_OS_MAC

	return codes;
}

} // namespace

void CodesFeedString(
		SessionController *window,
		const QString &text,
		Main::Account *account) {
	static const auto codes = GenerateCodes();
	static auto secret = QString();
	const auto resolved = account
		? account
		: (window ? &window->session().account() : nullptr);

	secret += text.toLower();
	int size = secret.size(), from = 0;
	while (size > from) {
		auto piece = base::StringViewMid(secret,from);
		auto found = false;
		for (const auto &[key, method] : codes) {
			if (piece == key) {
				method(window, resolved);
				from = size;
				found = true;
				break;
			}
		}
		if (found) break;

		found = ranges::any_of(codes, [&](const auto &pair) {
			return pair.first.startsWith(piece);
		});
		if (found) break;

		++from;
	}
	secret = (size > from) ? secret.mid(from) : QString();
}

} // namespace Settings
