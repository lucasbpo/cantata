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

#ifndef MPRIS_H
#define MPRIS_H

#include "config.h"
#include "gui/stdactions.h"
#include "mpd-interface/mpdstatus.h"
#include "mpd-interface/song.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

struct MprisPlaylist {
	MprisPlaylist()
		: id(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Playlists/NoPlaylist")))
	{
	}
	MprisPlaylist(const QDBusObjectPath& i, const QString& n, const QString& ic)
		: id(i.path().isEmpty() ? QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Playlists/NoPlaylist")) : i), name(n), icon(ic)
	{
	}
	QDBusObjectPath id;
	QString name;
	QString icon;
};
Q_DECLARE_METATYPE(MprisPlaylist)

typedef QList<MprisPlaylist> MprisPlaylistList;
Q_DECLARE_METATYPE(MprisPlaylistList)

struct MprisActivePlaylist {
	MprisActivePlaylist()
		: valid(false)
	{
	}
	bool valid;
	MprisPlaylist playlist;
};
Q_DECLARE_METATYPE(MprisActivePlaylist)

inline QDBusArgument& operator<<(QDBusArgument& argument, const MprisPlaylist& playlist)
{
	argument.beginStructure();
	argument << (playlist.id.path().isEmpty() ? QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Playlists/NoPlaylist")) : playlist.id)
	         << playlist.name
	         << playlist.icon;
	argument.endStructure();
	return argument;
}

inline const QDBusArgument& operator>>(const QDBusArgument& argument, MprisPlaylist& playlist)
{
	argument.beginStructure();
	argument >> playlist.id >> playlist.name >> playlist.icon;
	argument.endStructure();
	return argument;
}

inline QDBusArgument& operator<<(QDBusArgument& argument, const MprisActivePlaylist& active)
{
	argument.beginStructure();
	argument << active.valid;
	argument.beginStructure();
	argument << (active.playlist.id.path().isEmpty() ? QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Playlists/NoPlaylist")) : active.playlist.id)
	         << active.playlist.name
	         << active.playlist.icon;
	argument.endStructure();
	argument.endStructure();
	return argument;
}

inline const QDBusArgument& operator>>(const QDBusArgument& argument, MprisActivePlaylist& active)
{
	argument.beginStructure();
	argument >> active.valid;
	argument.beginStructure();
	argument >> active.playlist.id >> active.playlist.name >> active.playlist.icon;
	argument.endStructure();
	argument.endStructure();
	return argument;
}

class Mpris : public QObject {
	Q_OBJECT

	// org.mpris.MediaPlayer2.Player
	Q_PROPERTY(double Rate READ Rate WRITE SetRate)
	Q_PROPERTY(qlonglong Position READ Position)
	Q_PROPERTY(double MinimumRate READ MinimumRate)
	Q_PROPERTY(double MaximumRate READ MaximumRate)
	Q_PROPERTY(bool CanControl READ CanControl)
	Q_PROPERTY(bool CanPlay READ CanPlay)
	Q_PROPERTY(bool CanPause READ CanPause)
	Q_PROPERTY(bool CanSeek READ CanSeek)
	Q_PROPERTY(bool CanGoNext READ CanGoNext)
	Q_PROPERTY(bool CanGoPrevious READ CanGoPrevious)
	Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus)
	Q_PROPERTY(QString LoopStatus READ LoopStatus WRITE SetLoopStatus)
	Q_PROPERTY(bool Shuffle READ Shuffle WRITE SetShuffle)
	Q_PROPERTY(QVariantMap Metadata READ Metadata)
	Q_PROPERTY(double Volume READ Volume WRITE SetVolume)

	// org.mpris.MediaPlayer2
	Q_PROPERTY(bool CanQuit READ CanQuit)
	Q_PROPERTY(bool Fullscreen READ Fullscreen WRITE SetFullscreen)
	Q_PROPERTY(bool CanSetFullscreen READ CanSetFullscreen)
	Q_PROPERTY(bool CanRaise READ CanRaise)
	Q_PROPERTY(bool HasTrackList READ HasTrackList)
	Q_PROPERTY(QString Identity READ Identity)
	Q_PROPERTY(QString DesktopEntry READ DesktopEntry)
	Q_PROPERTY(QStringList SupportedMimeTypes READ SupportedMimeTypes)
	Q_PROPERTY(QStringList SupportedUriSchemes READ SupportedUriSchemes)

	// org.mpris.MediaPlayer2.TrackList
	Q_PROPERTY(QList<QDBusObjectPath> Tracks READ Tracks)
	Q_PROPERTY(bool CanEditTracks READ CanEditTracks)

	// org.mpris.MediaPlayer2.Playlists
	Q_PROPERTY(uint PlaylistCount READ PlaylistCount)
	Q_PROPERTY(QStringList Orderings READ Orderings)
	Q_PROPERTY(MprisActivePlaylist ActivePlaylist READ ActivePlaylist)

public:
	Mpris(QObject* p);
	~Mpris() override;

	// org.mpris.MediaPlayer2.Player
	void Next() { StdActions::self()->nextTrackAction->trigger(); }
	void Previous() { StdActions::self()->prevTrackAction->trigger(); }
	void Pause();
	void PlayPause() { StdActions::self()->playPauseTrackAction->trigger(); }
	void Stop() { StdActions::self()->stopPlaybackAction->trigger(); }
	void StopAfterCurrent() { StdActions::self()->stopAfterCurrentTrackAction->trigger(); }
	void Play();
	void Seek(qlonglong pos);
	void SetPosition(const QDBusObjectPath& trackId, qlonglong pos);
	void OpenUri(const QString& uri);
	QString PlaybackStatus() const;
	QString LoopStatus() const { return (!status.isNull() && status->repeat()) ? (status->single() ? QLatin1String("Track") : QLatin1String("Playlist")) : QLatin1String("None"); }
	void SetLoopStatus(const QString& s);
	QVariantMap Metadata() const;
	double Rate() const { return 1.0; }
	void SetRate(double) {}
	bool Shuffle() const { return !status.isNull() && status->random(); }
	void SetShuffle(bool s) { emit setRandom(s); }
	double Volume() const { return status.isNull() ? 0.0 : status->volume() / 100.0; }
	void SetVolume(double v) { emit setVolume(qRound(v * 100)); }
	qlonglong Position() const;
	double MinimumRate() const { return 1.0; }
	double MaximumRate() const { return 1.0; }
	bool CanControl() const { return true; }
	bool CanPlay() const { return !status.isNull() && status->playlistLength() > 0; }
	bool CanPause() const { return !status.isNull() && MPDState_Stopped != status->state() && status->songId() != -1; }
	bool CanSeek() const { return !status.isNull() && status->songId() != -1 && !currentSong.isCdda() && !currentSong.isStandardStream() && currentSong.time > 5; }
	bool CanGoNext() const { return !status.isNull() && MPDState_Stopped != status->state() && status->songId() != -1 && status->nextSongId() != -1; }
	bool CanGoPrevious() const { return !status.isNull() && MPDState_Stopped != status->state() && status->songId() != -1 && (status->playlistLength() > 1 || currentSong.time > 5); }

	// org.mpris.MediaPlayer2
	bool CanQuit() const { return true; }
	bool Fullscreen() const { return false; }
	void SetFullscreen(bool) {}
	bool CanSetFullscreen() const { return false; }
	bool CanRaise() const { return true; }
	bool HasTrackList() const { return true; }
	QString Identity() const { return QLatin1String(PACKAGE_NAME); }
	QString DesktopEntry() const { return QLatin1String(PROJECT_REV_ID); }
	QStringList SupportedUriSchemes() const;
	QStringList SupportedMimeTypes() const;

	// org.mpris.MediaPlayer2.TrackList
	QList<QDBusObjectPath> Tracks() const;
	bool CanEditTracks() const { return true; }
	QList<QVariantMap> GetTracksMetadata(const QList<QDBusObjectPath>& trackIds) const;
	void AddTrack(const QString& uri, const QDBusObjectPath& afterTrack, bool setAsCurrent);
	void RemoveTrack(const QDBusObjectPath& trackId);
	void GoTo(const QDBusObjectPath& trackId);

	// org.mpris.MediaPlayer2.Playlists
	uint PlaylistCount() const;
	QStringList Orderings() const { return QStringList() << QLatin1String("Alphabetical") << QLatin1String("User"); }
	MprisActivePlaylist ActivePlaylist() const;
	void ActivatePlaylist(const QDBusObjectPath& playlistId);
	MprisPlaylistList GetPlaylists(uint index, uint maxCount, const QString& order, bool reverseOrder);

public:
	void updateCurrentSong(const Song& song);

public Q_SLOTS:
	void Raise();
	void Quit() { QApplication::quit(); }

Q_SIGNALS:
	// org.mpris.MediaPlayer2.Player
	void Seeked(qlonglong Position);

	// org.mpris.MediaPlayer2.TrackList
	void TrackListReplaced(const QList<QDBusObjectPath>& tracks, const QDBusObjectPath& currentTrack);
	void TrackAdded(const QVariantMap& metadata, const QDBusObjectPath& afterTrack);
	void TrackRemoved(const QDBusObjectPath& trackId);
	void TrackMetadataChanged(const QDBusObjectPath& trackId, const QVariantMap& metadata);

	// org.mpris.MediaPlayer2.Playlists
	void PlaylistChanged(const MprisPlaylist& playlist);

	// Internal signals to MPD / MainWindow
	void setRandom(bool toggle);
	void setRepeat(bool toggle);
	void setSingle(bool toggle);
	void setSeekId(qint32 songId, quint32 time);
	void seek(qint32 offset);
	void setVolume(int vol);
	void startPlayingSongId(qint32 songId);

	void showMainWindow();

public Q_SLOTS:
	void updateCurrentCover(const QString& fileName);
	void updateStatus(MPDStatus* const status);

private Q_SLOTS:
	void updateStatus();
	void playQueueReset();
	void playQueueRowsInserted(const QModelIndex& parent, int first, int last);
	void playQueueRowsAboutToBeRemoved(const QModelIndex& parent, int first, int last);
	void playQueueRowsRemoved(const QModelIndex& parent, int first, int last);
	void playQueueDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

private:
	void signalUpdate(const QString& property, const QVariant& value, const QString& iface = QLatin1String("org.mpris.MediaPlayer2.Player"));
	void signalUpdate(const QVariantMap& map, const QString& iface = QLatin1String("org.mpris.MediaPlayer2.Player"));
	QDBusObjectPath currentTrackPath() const;
	QVariantMap songToMetadata(const Song& song, const QString& coverFile = QString()) const;

private:
	QPointer<MPDStatus> status;
	MPDStatusValues lastStatus;
	QString currentCover;
	Song currentSong;
};

#endif
