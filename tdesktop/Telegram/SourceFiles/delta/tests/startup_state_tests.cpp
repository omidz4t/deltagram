#include "startup_state.h"

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

class StartupStateTests final : public QObject {
	Q_OBJECT

private slots:
	void loadingAndFailureCannotCreate() {
		Delta::StartupState state;
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::None);
		QVERIFY(!state.canCreate());
		QVERIFY(!state.nextEnabled());
		QCOMPARE(state.nextLabel(true), u"Loading…"_s);
		QCOMPARE(state.knownAccountId, 0);

		const auto first = state.beginDiscovery();
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::None);
		QVERIFY(!state.canCreate());
		QVERIFY(!state.live(0));
		QVERIFY(state.live(first));

		state.failDiscovery(u"Could not read profile state. Please try again."_s);
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::RetryDiscovery);
		QVERIFY(!state.canCreate());
		QVERIFY(state.nextEnabled());
		QCOMPARE(state.nextLabel(true), u"Retry"_s);
		QCOMPARE(state.knownAccountId, 0);
		QVERIFY(state.live(first));

		const auto retry = state.beginDiscovery();
		QVERIFY(retry != first);
		QVERIFY(!state.live(first));
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::None);
		QVERIFY(!state.canCreate());
		QCOMPARE(state.knownAccountId, 0);
	}

	void configuredFailRetriesKnownId() {
		Delta::StartupState state;
		state.beginDiscovery();
		state.readyConfigured(1);
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::OpenKnown);
		QVERIFY(!state.canCreate());
		QCOMPARE(state.knownAccountId, 1);
		QCOMPARE(state.nextLabel(true), u"Open Profile"_s);
		QCOMPARE(state.nextLabel(false), u"Open Profile"_s);

		state.failOpen(u"start_io rejected"_s);
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::OpenKnown);
		QVERIFY(!state.canCreate());
		QCOMPARE(state.knownAccountId, 1);
		QVERIFY(!state.completedOpen());
		QCOMPARE(state.nextLabel(true), u"Open Profile"_s);
		QCOMPARE(state.error, u"start_io rejected"_s);
	}

	void selectionFailDoesNotOpenWrongId() {
		const auto configured = QVector<int>{ 1, 2 };
		const auto failed = Delta::ReadSelectedAccount({
			{ u"error"_s, u"offline"_s },
		});
		QVERIFY(!failed.ok);
		QCOMPARE(failed.selectedId, 0);
		QCOMPARE(Delta::ConfiguredOpenIdOrZero(configured, failed), 0);

		const auto malformed = Delta::ReadSelectedAccount({
			{ u"result"_s, 1.5 },
		});
		QVERIFY(!malformed.ok);
		QCOMPARE(Delta::ConfiguredOpenIdOrZero(configured, malformed), 0);

		const auto absent = Delta::ReadSelectedAccount({});
		QVERIFY(!absent.ok);
		QCOMPARE(Delta::ConfiguredOpenIdOrZero(configured, absent), 0);

		const auto none = Delta::ReadSelectedAccount({
			{ u"result"_s, QJsonValue(QJsonValue::Null) },
		});
		QVERIFY(none.ok);
		QCOMPARE(none.selectedId, 0);
		QCOMPARE(Delta::ConfiguredOpenIdOrZero(configured, none), 1);

		const auto selected = Delta::ReadSelectedAccount({
			{ u"result"_s, 2 },
		});
		QVERIFY(selected.ok);
		QCOMPARE(selected.selectedId, 2);
		QCOMPARE(Delta::ConfiguredOpenIdOrZero(configured, selected), 2);

		Delta::StartupState state;
		state.beginDiscovery();
		state.failDiscovery(failed.error);
		QCOMPARE(state.knownAccountId, 0);
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::RetryDiscovery);
		QVERIFY(!state.canCreate());
	}

	void completedKeyboardMatchesNext() {
		Delta::StartupState state;
		state.markLiveAdd();
		state.markCompleted(7);
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::OpenKnown);
		QCOMPARE(state.knownAccountId, 7);
		QVERIFY(state.completedOpen());
		QVERIFY(!state.canCreate());
		QCOMPARE(state.nextLabel(true), u"Open Profile"_s);
		QCOMPARE(state.nextLabel(false), u"Open Profile"_s);
		QVERIFY(state.nextEnabled());

		state.failOpen(u"start_io rejected"_s);
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::OpenKnown);
		QCOMPARE(state.nextLabel(true), state.nextLabel(false));
		QCOMPARE(state.knownAccountId, 7);
	}

	void liveAddStillCreates() {
		Delta::StartupState state;
		state.markLiveAdd();
		QCOMPARE(state.primaryAction(), Delta::StartupState::Action::Create);
		QVERIFY(state.canCreate());
		QVERIFY(state.nextEnabled());
		QCOMPARE(state.nextLabel(true), u"Create New Profile"_s);
		QCOMPARE(state.nextLabel(false), u"Agree & Create Profile"_s);
		QCOMPARE(state.knownAccountId, 0);
		QVERIFY(!state.completedOpen());
	}

};

QTEST_GUILESS_MAIN(StartupStateTests)
#include "startup_state_tests.moc"
