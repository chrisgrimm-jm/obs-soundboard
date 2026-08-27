#pragma once

#include <QObject>
#include <QTcpServer>
#include <QByteArray>
#include <QString>
#include <string>

class QTcpSocket;

class CompanionServer : public QObject {
	Q_OBJECT

public:
	explicit CompanionServer(QObject *parent = nullptr);
	~CompanionServer() override;

	bool start(quint16 port);
	void stop();
	quint16 port() const { return m_port; }

private slots:
	void onNewConnection();
	void onReadyRead();
	void onClientDisconnected();

private:
	struct RequestLine {
		QString method;
		QString path;
	};
	static RequestLine parseRequestLine(const QByteArray &data);

	void handleRequest(QTcpSocket *sock, const QString &method, const QString &path);

	void sendJson(QTcpSocket *sock, int status, const QByteArray &body);
	void sendError(QTcpSocket *sock, int status, const QString &msg);

	QByteArray buildStatusJson() const;
	static QByteArray buildClipJson(const std::string &name, bool playing);
	static QByteArray escapeJson(const std::string &s);

	QTcpServer *m_server = nullptr;
	quint16 m_port = 4489;
};

// Global pointer used by SoundboardSettings to restart the server on port change
extern CompanionServer *g_companionServer;
