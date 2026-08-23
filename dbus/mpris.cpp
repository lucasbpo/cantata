/*
 * Cantata
 *
 * Copyright (c) 2011-2022 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "mpris.h"
#include "config.h"
#include "gui/currentcover.h"
#include "models/playqueuemodel.h"
#include "models/playlistsmodel.h"
#include "mpd-interface/mpdconnection.h"
#include "playeradaptor.h"
#include "playlistsadaptor.h"
#include "rootadaptor.h"
#include "tracklistadaptor.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QUrl>
#include <QWidget>
#include <algorithm>

static inline qlonglong convertTime(qlonglong t)
{
	return t * 1000000;
}

static inline QDBusObjectPath trackIdToPath(qint32 songId)
{
	if (songId < 0) {
		return QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack");
	}
	return QDBusObjectPath(QString("/org/mpris/MediaPlayer2/TrackList/Track_%1").arg(songId));
}

static inline qint32 pathToTrackId(const QDBusObjectPath& path)
{
	QString p = path.path();
	if (p.endsWith(QLatin1String("/NoTrack"))) {
		return -1;
	}
	int idx = p.lastIndexOf(QLatin1Char('_'));
	if (idx < 0) {
		idx = p.lastIndexOf(QLatin1Char('/'));
	}
	if (idx < 0) {
		return -1;
	}
	return p.mid(idx + 1).toInt();
}

static inline QDBusObjectPath playlistToPath(const QString& name)
{
	QByteArray hex = name.toUtf8().toHex();
	return QDBusObjectPath(QString("/org/mpris/MediaPlayer2/Playlists/PL_%1").arg(QString::fromLatin1(hex)));
}

static inline QString pathToPlaylist(const QDBusObjectPath& path)
{
	QString p = path.path();
	int idx = p.lastIndexOf(QLatin1String("PL_"));
	if (idx < 0) {
		return QString();
	}
	QByteArray hex = p.mid(idx + 3).toLatin1();
	return QString::fromUtf8(QByteArray::fromHex(hex));
}

Mpris::Mpris(QObject* p)
	: QObject(p)
{
	qRegisterMetaType<MprisPlaylist>("MprisPlaylist");
	qDBusRegisterMetaType<MprisPlaylist>();
	qRegisterMetaType<MprisPlaylistList>("MprisPlaylistList");
	qDBusRegisterMetaType<MprisPlaylistList>();
	qRegisterMetaType<MprisActivePlaylist>("MprisActivePlaylist");
	qDBusRegisterMetaType<MprisActivePlaylist>();
	qDBusRegisterMetaType<QList<QVariantMap>>();

	QDBusConnection::sessionBus().registerService(QStringLiteral("org.mpris.MediaPlayer2.") + QStringLiteral(PROJECT_REV_ID));
	QDBusConnection::sessionBus().registerService(QStringLiteral("org.mpris.MediaPlayer2.cantata"));

	new PlayerAdaptor(this);
	new MediaPlayer2Adaptor(this);
	new TrackListAdaptor(this);
	new PlaylistsAdaptor(this);

	QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this, QDBusConnection::ExportAdaptors);
	connect(this, SIGNAL(setRandom(bool)), MPDConnection::self(), SLOT(setRandom(bool)));
	connect(this, SIGNAL(setRepeat(bool)), MPDConnection::self(), SLOT(setRepeat(bool)));
	connect(this, SIGNAL(setSingle(bool)), MPDConnection::self(), SLOT(setSingle(bool)));
	connect(this, SIGNAL(setSeekId(qint32, quint32)), MPDConnection::self(), SLOT(setSeekId(qint32, quint32)));
	connect(this, SIGNAL(seek(qint32)), MPDConnection::self(), SLOT(seek(qint32)));
	connect(this, SIGNAL(setVolume(int)), MPDConnection::self(), SLOT(setVolume(int)));
	connect(this, SIGNAL(startPlayingSongId(qint32)), MPDConnection::self(), SLOT(startPlayingSongId(qint32)));

	connect(CurrentCover::self(), SIGNAL(coverFile(const QString&)), this, SLOT(updateCurrentCover(const QString&)));

	connect(PlayQueueModel::self(), SIGNAL(modelReset()), this, SLOT(playQueueReset()));
	connect(PlayQueueModel::self(), SIGNAL(rowsInserted(const QModelIndex&, int, int)), this, SLOT(playQueueRowsInserted(const QModelIndex&, int, int)));
	connect(PlayQueueModel::self(), SIGNAL(rowsRemoved(const QModelIndex&, int, int)), this, SLOT(playQueueRowsRemoved(const QModelIndex&, int, int)));
	connect(PlayQueueModel::self(), SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)), this, SLOT(playQueueDataChanged(const QModelIndex&, const QModelIndex&)));
}

Mpris::~Mpris()
{
	QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.mpris.MediaPlayer2.cantata"));
	QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.mpris.MediaPlayer2.") + QStringLiteral(PROJECT_REV_ID));
}

void Mpris::Pause()
{
	if (!status.isNull() && MPDState_Playing == status->state()) {
		StdActions::self()->playPauseTrackAction->trigger();
	}
}

void Mpris::Play()
{
	if (!status.isNull() && status->playlistLength() > 0 && MPDState_Playing != status->state()) {
		StdActions::self()->playPauseTrackAction->trigger();
	}
}

void Mpris::Seek(qlonglong pos)
{
	qint32 offsetSeconds = pos / 1000000;
	emit seek(offsetSeconds);
	if (!status.isNull()) {
		emit Seeked(convertTime(status->guessedElapsed() + offsetSeconds));
	}
}

void Mpris::SetPosition(const QDBusObjectPath& trackId, qlonglong pos)
{
	if (trackId == currentTrackPath() || pathToTrackId(trackId) == currentSong.id) {
		emit setSeekId(-1, pos / 1000000);
		emit Seeked(pos);
	}
}

void Mpris::OpenUri(const QString& uri)
{
	if (uri.isEmpty()) {
		return;
	}
	QString decodedUri = uri;
	if (decodedUri.startsWith(QLatin1String("file://"))) {
		decodedUri = QUrl(uri).toLocalFile();
	}
	PlayQueueModel::self()->load(QStringList() << decodedUri, -1);
	if (!status.isNull() && MPDState_Playing != status->state()) {
		StdActions::self()->playPauseTrackAction->trigger();
	}
}

QString Mpris::PlaybackStatus() const
{
	if (status.isNull()) {
		return QLatin1String("Stopped");
	}

	switch (status->state()) {
	case MPDState_Playing: return QLatin1String("Playing");
	case MPDState_Paused: return QLatin1String("Paused");
	default:
	case MPDState_Stopped: return QLatin1String("Stopped");
	}
}

void Mpris::SetLoopStatus(const QString& s)
{
	if (status.isNull()) {
		return;
	}

	bool repeat = (QLatin1String("None") != s);
	bool single = (QLatin1String("Track") == s);

	if (status->repeat() != repeat) {
		emit setRepeat(repeat);
	}
	if (status->single() != single) {
		emit setSingle(single);
	}
}

qlonglong Mpris::Position() const
{
	return status.isNull() ? 0 : convertTime(status->guessedElapsed());
}

bool Mpris::Fullscreen() const
{
	QWidget* pw = qobject_cast<QWidget*>(parent());
	return pw ? pw->isFullScreen() : false;
}

void Mpris::SetFullscreen(bool f)
{
	emit setWindowFullscreen(f);
}

QStringList Mpris::SupportedUriSchemes() const
{
	return QStringList() << QLatin1String("file") << QLatin1String("http") << QLatin1String("https") << QLatin1String("cdda");
}

QStringList Mpris::SupportedMimeTypes() const
{
	return QStringList()
			<< QLatin1String("audio/mpeg")
			<< QLatin1String("audio/ogg")
			<< QLatin1String("audio/x-vorbis+ogg")
			<< QLatin1String("audio/flac")
			<< QLatin1String("audio/x-flac")
			<< QLatin1String("audio/aac")
			<< QLatin1String("audio/mp4")
			<< QLatin1String("audio/x-wav")
			<< QLatin1String("audio/wav")
			<< QLatin1String("audio/x-matroska")
			<< QLatin1String("audio/x-aiff")
			<< QLatin1String("audio/opus")
			<< QLatin1String("application/ogg")
			<< QLatin1String("application/x-ogg");
}

QList<QDBusObjectPath> Mpris::Tracks() const
{
	QList<QDBusObjectPath> list;
	PlayQueueModel* pq = PlayQueueModel::self();
	int count = pq->rowCount();
	list.reserve(count);
	for (int i = 0; i < count; ++i) {
		qint32 id = pq->getIdByRow(i);
		list.append(trackIdToPath(id));
	}
	return list;
}

QList<QVariantMap> Mpris::GetTracksMetadata(const QList<QDBusObjectPath>& trackIds) const
{
	QList<QVariantMap> result;
	PlayQueueModel* pq = PlayQueueModel::self();
	for (const QDBusObjectPath& path : trackIds) {
		qint32 id = pathToTrackId(path);
		if (id >= 0) {
			Song s = pq->getSongById(id);
			if (!s.file.isEmpty() || !s.title.isEmpty()) {
				result.append(songToMetadata(s));
				continue;
			}
		}
		result.append(QVariantMap());
	}
	return result;
}

void Mpris::AddTrack(const QString& uri, const QDBusObjectPath& afterTrack, bool setAsCurrent)
{
	PlayQueueModel* pq = PlayQueueModel::self();
	qint32 afterId = pathToTrackId(afterTrack);
	int row = (afterId >= 0) ? (pq->getRowById(afterId) + 1) : -1;
	QString decodedUri = uri;
	if (decodedUri.startsWith(QLatin1String("file://"))) {
		decodedUri = QUrl(uri).toLocalFile();
	}
	pq->load(QStringList() << decodedUri, row);
	if (setAsCurrent && row >= 0) {
		qint32 newId = pq->getIdByRow(row);
		if (newId >= 0) {
			emit startPlayingSongId(newId);
		}
	}
}

void Mpris::RemoveTrack(const QDBusObjectPath& trackId)
{
	qint32 id = pathToTrackId(trackId);
	if (id >= 0) {
		PlayQueueModel* pq = PlayQueueModel::self();
		int row = pq->getRowById(id);
		if (row >= 0) {
			pq->removeSongs(QList<qint32>() << row);
		}
	}
}

void Mpris::GoTo(const QDBusObjectPath& trackId)
{
	qint32 id = pathToTrackId(trackId);
	if (id >= 0) {
		emit startPlayingSongId(id);
	}
}

uint Mpris::PlaylistCount() const
{
	return PlaylistsModel::self()->rowCount();
}

MprisActivePlaylist Mpris::ActivePlaylist() const
{
	MprisActivePlaylist active;
	active.valid = false;
	return active;
}

void Mpris::ActivatePlaylist(const QDBusObjectPath& playlistId)
{
	QString name = pathToPlaylist(playlistId);
	if (!name.isEmpty()) {
		MPDConnection::self()->loadPlaylist(name, true);
		MPDConnection::self()->play();
	}
}

MprisPlaylistList Mpris::GetPlaylists(uint index, uint maxCount, const QString& order, bool reverseOrder)
{
	Q_UNUSED(order)
	MprisPlaylistList result;
	PlaylistsModel* pm = PlaylistsModel::self();
	int count = pm->rowCount();
	if (count <= 0 || index >= (uint)count) {
		return result;
	}

	QList<int> indices;
	for (int i = 0; i < count; ++i) {
		indices.append(i);
	}
	if (reverseOrder) {
		std::reverse(indices.begin(), indices.end());
	}

	uint taken = 0;
	for (uint i = index; i < (uint)indices.count() && taken < maxCount; ++i, ++taken) {
		int row = indices.at(i);
		QModelIndex idx = pm->index(row, 0, QModelIndex());
		QString name = pm->data(idx, Qt::DisplayRole).toString();
		MprisPlaylist pl;
		pl.id = playlistToPath(name);
		pl.name = name;
		pl.icon = QLatin1String("audio-x-generic");
		result.append(pl);
	}
	return result;
}

void Mpris::playQueueReset()
{
	QList<QDBusObjectPath> tracks = Tracks();
	emit TrackListReplaced(tracks, currentTrackPath());
	signalUpdate(QLatin1String("Tracks"), QVariant::fromValue(tracks), QLatin1String("org.mpris.MediaPlayer2.TrackList"));
}

void Mpris::playQueueRowsInserted(const QModelIndex& /*parent*/, int first, int last)
{
	PlayQueueModel* pq = PlayQueueModel::self();
	for (int i = first; i <= last; ++i) {
		Song s = pq->getSongByRow(i);
		qint32 prevId = (i > 0) ? pq->getIdByRow(i - 1) : -1;
		emit TrackAdded(songToMetadata(s), trackIdToPath(prevId));
	}
	signalUpdate(QLatin1String("Tracks"), QVariant::fromValue(Tracks()), QLatin1String("org.mpris.MediaPlayer2.TrackList"));
}

void Mpris::playQueueRowsRemoved(const QModelIndex& /*parent*/, int first, int last)
{
	Q_UNUSED(first)
	Q_UNUSED(last)
	signalUpdate(QLatin1String("Tracks"), QVariant::fromValue(Tracks()), QLatin1String("org.mpris.MediaPlayer2.TrackList"));
}

void Mpris::playQueueDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
	PlayQueueModel* pq = PlayQueueModel::self();
	for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
		Song s = pq->getSongByRow(row);
		emit TrackMetadataChanged(trackIdToPath(s.id), songToMetadata(s));
	}
}

void Mpris::updateStatus()
{
	updateStatus(MPDStatus::self());
}

void Mpris::updateStatus(MPDStatus* const status)
{
	QVariantMap map;

	if (status->songId() != currentSong.id && status->songId() != -1) {
		return;
	}

	if (status != this->status) {
		this->status = status;
	}
	if (status->repeat() != lastStatus.repeat || status->single() != lastStatus.single) {
		map.insert("LoopStatus", LoopStatus());
	}
	if (status->random() != lastStatus.random) {
		map.insert("Shuffle", Shuffle());
	}
	if (status->volume() != lastStatus.volume) {
		map.insert("Volume", Volume());
	}
	if (status->state() != lastStatus.state || status->songId() != lastStatus.songId || status->nextSongId() != lastStatus.nextSongId || status->playlistLength() != lastStatus.playlistLength) {
		map.insert("CanGoNext", CanGoNext());
		map.insert("CanGoPrevious", CanGoPrevious());
	}
	if (status->state() != lastStatus.state) {
		map.insert("PlaybackStatus", PlaybackStatus());
		map.insert("CanPause", CanPause());
	}
	if (status->playlistLength() != lastStatus.playlistLength) {
		map.insert("CanPlay", CanPlay());
	}
	if (status->songId() != lastStatus.songId || status->timeTotal() != lastStatus.timeTotal) {
		map.insert("CanSeek", CanSeek());
	}
	if (status->timeElapsed() != lastStatus.timeElapsed) {
		map.insert("Position", convertTime(status->timeElapsed()));
		if (status->songId() == lastStatus.songId && lastStatus.songId != -1 && status->state() == MPDState_Playing) {
			int delta = qAbs((int)status->timeElapsed() - (int)lastStatus.timeElapsed);
			if (delta > 2) {
				emit Seeked(convertTime(status->timeElapsed()));
			}
		}
	}
	if (!map.isEmpty() || status->songId() != lastStatus.songId) {
		if (!map.contains("Position")) {
			map.insert("Position", convertTime(status->timeElapsed()));
		}
		map.insert("Metadata", Metadata());
		signalUpdate(map);
	}

	lastStatus = status->getValues();
}

void Mpris::updateCurrentCover(const QString& fileName)
{
	if (fileName != currentCover) {
		currentCover = fileName;
		signalUpdate("Metadata", Metadata());
	}
}

void Mpris::updateCurrentSong(const Song& song)
{
	qint32 lastSongId = currentSong.id;
	currentSong = song;

	if (song.id != lastSongId && (song.id == lastStatus.songId || -1 == lastStatus.songId)) {
		if (-1 != lastStatus.songId) {
			lastStatus.songId = lastSongId;
		}
		updateStatus();
	}
	else {
		signalUpdate("Metadata", Metadata());
	}
}

QVariantMap Mpris::songToMetadata(const Song& song, const QString& coverFile) const
{
	QVariantMap metadataMap;
	if ((!song.title.isEmpty() && !song.artist.isEmpty()) || (song.isStandardStream() && !song.name().isEmpty())) {
		metadataMap.insert("mpris:trackid", QVariant::fromValue(trackIdToPath(song.id)));
		QString artist = song.artist;
		QString album = song.album;
		QString title = song.title;

		if (song.isStandardStream()) {
			if (artist.isEmpty()) {
				artist = song.name();
			}
			else if (album.isEmpty()) {
				album = song.name();
			}
			if (title.isEmpty()) {
				title = tr("(Stream)");
			}
		}
		if (song.time > 0) {
			metadataMap.insert("mpris:length", convertTime(song.time));
		}
		if (!album.isEmpty()) {
			metadataMap.insert("xesam:album", album);
		}
		if (!song.albumartist.isEmpty() && song.albumartist != song.artist) {
			metadataMap.insert("xesam:albumArtist", QStringList() << song.albumartist);
		}
		if (!artist.isEmpty()) {
			metadataMap.insert("xesam:artist", QStringList() << artist);
		}
		if (!title.isEmpty()) {
			metadataMap.insert("xesam:title", title);
		}
		if (!song.genres[0].isEmpty()) {
			metadataMap.insert("xesam:genre", QStringList() << song.genres[0]);
		}
		if (song.track > 0) {
			metadataMap.insert("xesam:trackNumber", song.track);
		}
		if (song.disc > 0) {
			metadataMap.insert("xesam:discNumber", song.disc);
		}
		if (song.year > 0) {
			metadataMap.insert("xesam:contentCreated", QString("%1").arg(song.year, 4, 10, QChar(u'0')));
		}
		if (song.rating > 0 && song.rating <= Song::Rating_Max) {
			metadataMap.insert("xesam:userRating", (double)song.rating / (double)Song::Rating_Max);
		}
		if (!song.comment().isEmpty()) {
			metadataMap.insert("xesam:comment", QStringList() << song.comment());
		}
		if (!song.composer().isEmpty()) {
			metadataMap.insert("xesam:composer", QStringList() << song.composer());
		}
		if (!song.file.isEmpty()) {
			if (song.isNonMPD()) {
				metadataMap.insert("xesam:url", song.file);
			}
			else if (MPDConnection::self()->getDetails().dirReadable) {
				QString mpdDir = MPDConnection::self()->getDetails().dir;
				if (!mpdDir.isEmpty()) {
					metadataMap.insert("xesam:url", "file://" + mpdDir + song.file);
				}
			}
		}
		if (!coverFile.isEmpty()) {
			metadataMap.insert("mpris:artUrl", "file://" + coverFile);
		}
		else if (!currentCover.isEmpty() && song.id == currentSong.id) {
			metadataMap.insert("mpris:artUrl", "file://" + currentCover);
		}
	}
	return metadataMap;
}

QVariantMap Mpris::Metadata() const
{
	return songToMetadata(currentSong, currentCover);
}

void Mpris::Raise()
{
	emit showMainWindow();
}

void Mpris::signalUpdate(const QString& property, const QVariant& value, const QString& iface)
{
	QVariantMap map;
	map.insert(property, value);
	signalUpdate(map, iface);
}

void Mpris::signalUpdate(const QVariantMap& map, const QString& iface)
{
	if (map.isEmpty()) {
		return;
	}
	QDBusMessage signal = QDBusMessage::createSignal("/org/mpris/MediaPlayer2",
	                                                 "org.freedesktop.DBus.Properties",
	                                                 "PropertiesChanged");
	QVariantList args = QVariantList()
			<< iface
			<< map
			<< QStringList();
	signal.setArguments(args);
	QDBusConnection::sessionBus().send(signal);
}

QDBusObjectPath Mpris::currentTrackPath() const
{
	return trackIdToPath(currentSong.id);
}

#include "moc_mpris.cpp"
