#pragma once

#include <QByteArray>
#include <QDir>
#include <QFileDevice>
#include <QFileInfo>
#include <QString>
#include <QTemporaryFile>

#include <memory>
#include <utility>

namespace Delta {

class ProfileSetupAvatarFile final {
public:
	explicit ProfileSetupAvatarFile(std::unique_ptr<QTemporaryFile> file)
	: _file(std::move(file)) {
	}
	ProfileSetupAvatarFile(const ProfileSetupAvatarFile &) = delete;
	ProfileSetupAvatarFile &operator=(const ProfileSetupAvatarFile &) = delete;

	[[nodiscard]] QString path() const {
		return _file->fileName();
	}

private:
	std::unique_ptr<QTemporaryFile> _file;

};

// WHY: concurrent setups must not share one temp avatar path before Core copies it.
[[nodiscard]] inline std::shared_ptr<ProfileSetupAvatarFile>
StageProfileSetupAvatar(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return nullptr;
	}
	auto file = std::make_unique<QTemporaryFile>(
		QDir::temp().filePath(
			QStringLiteral("delta-tel-setup-avatar-XXXXXX.png")));
	file->setAutoRemove(true);
	if (!file->open()) {
		return nullptr;
	}
	if (!file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
		|| file->write(bytes) != bytes.size()
		|| !file->flush()) {
		file->remove();
		return nullptr;
	}
	const auto path = file->fileName();
	file->close();
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		file->remove();
		return nullptr;
	}
	return std::make_shared<ProfileSetupAvatarFile>(std::move(file));
}

} // namespace Delta
