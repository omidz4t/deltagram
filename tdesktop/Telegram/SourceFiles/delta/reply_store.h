#pragma once

#include "delta_rpc.h"

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace Delta {

enum class ReplyState { Pending, Attempted, Sent };

struct SavedReply {
	QString id;
	int chat = 0;
	QString text;
	ReplyState state = ReplyState::Pending;
	int message = 0;
	bool durable = false;
	QString chatName;
	int userId = 0;
};

class ReplyStore final {
public:
	using Call = std::function<void(QString, QJsonArray, DeltaRpc::Callback)>;
	using Done = std::function<void(int, QString)>;
	using Listed = std::function<void(std::vector<SavedReply>, QString)>;
	ReplyStore(Call call, std::function<void()> changed);
	ReplyStore(const ReplyStore&) = delete;
	ReplyStore &operator=(const ReplyStore&) = delete;
	void submit(int account, int chat, QString text, Done done);
	void load(int account, Listed done);
	void send(int account, SavedReply reviewed, Done done);
	void discard(int account, SavedReply reviewed, Done done);
	[[nodiscard]] bool hasUnsaved(int account = 0) const;

private:
	using Key = std::pair<int, QString>;
	using ReadDone = std::function<void(std::optional<SavedReply>, QString)>;
	struct Listing;
	void call(QString method, QJsonArray params, DeltaRpc::Callback done);
	void read(Key key, ReadDone done);
	void write(int account, SavedReply reply, std::function<void(bool, QString)> done);
	void clear(int account, SavedReply reply, Done done);
	void attempt(int account, SavedReply reply, Done done);
	void listNext(std::shared_ptr<Listing> state);
	bool begin(Key key, const Done &done);
	void finish(Key key, int message, QString error, Done done);
	Call _call;
	std::function<void()> _changed;
	std::map<Key, SavedReply> _unsaved;
	std::set<Key> _busy;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

}
