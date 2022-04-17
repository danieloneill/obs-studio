/******************************************************************************
    Copyright (C) 2015 by Hugh Bailey <obs.jim@gmail.com>
    Copyright (C) 2022 by Daniel O'Neill <daniel@oneill.app>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "obs-app.hpp"
#include "qt-wrappers.hpp"
#include "remote-text.hpp"

#include <QEventLoop>
#include <QUrlQuery>

void RemoteText::start()
{
	buffer.clear();

	QNetworkRequest request(QUrl(QString::fromStdString(url)));
	QString version = QString::fromStdString(App()->GetVersionString());
	request.setHeader(QNetworkRequest::UserAgentHeader,
			  QString("User-Agent: obs-basic %1")
				.arg(version));
	if (!contentType.empty())
		request.setHeader(QNetworkRequest::ContentTypeHeader,
				  QString::fromStdString(contentType));

	if (timeoutSec)
		request.setTransferTimeout(1000*timeoutSec);

	for (std::string &h : extraHeaders) {
		std::string::size_type loc = h.find(": ");
		if (loc == std::string::npos)
			continue;

		QByteArray name = QByteArray::fromStdString(h.substr(0, loc));
		QByteArray value = QByteArray::fromStdString(h.substr(loc+2));
		request.setRawHeader(name, value);
	}

	if (!postData.empty()) {
		if (contentType.empty())
			request.setHeader(QNetworkRequest::ContentTypeHeader,
					  "application/x-www-form-urlencoded");

		QByteArray postDataBA = QByteArray::fromStdString(postData);
		reply = qnam.post(request, postDataBA);
	} else
		reply = qnam.get(request);

	connect(reply, &QNetworkReply::finished,
		this, &RemoteText::slotHttpFinished);

	connect(reply, &QNetworkReply::errorOccurred,
		this, &RemoteText::slotHttpError);

	connect(reply, &QIODevice::readyRead,
		this, &RemoteText::slotHttpReadyRead);
}

void RemoteText::slotHttpReadyRead()
{
	buffer.append(reply->readAll());
}

void RemoteText::slotHttpFinished()
{
	emit Result(QString::fromUtf8(buffer), QString());
}

void RemoteText::slotHttpError(QNetworkReply::NetworkError code)
{
	blog(LOG_WARNING,
	     "RemoteText: HTTP request failed. %s",
	     reply->errorString().toStdString().c_str());
	emit Result(QString(), reply->errorString());
}

bool GetRemoteFile(const char *url, std::string &str, std::string &error,
		   long *responseCode, const char *contentType,
		   std::string request_type, const char *postData,
		   std::vector<std::string> extraHeaders,
		   std::string *signature, int timeoutSec, bool fail_on_error,
		   int postDataSize)
{
	QEventLoop loop;
	QByteArray buffer;
	QString errorString;
	bool in_error = false;

	QNetworkAccessManager qnam;
	QNetworkReply *reply;
	QNetworkRequest request(QUrl(QString::fromStdString(url)));

	QString version = QString::fromStdString(App()->GetVersionString());
	request.setHeader(QNetworkRequest::UserAgentHeader,
			  QString("User-Agent: obs-basic %1")
				.arg(version));
	if (contentType)
		request.setHeader(QNetworkRequest::ContentTypeHeader,
				  QString::fromUtf8(contentType));

	if (timeoutSec)
		request.setTransferTimeout(1000*timeoutSec);

	for (std::string &h : extraHeaders) {
		std::string::size_type loc = h.find(": ");
		if (loc == std::string::npos)
			continue;

		QByteArray name = QByteArray::fromStdString(h.substr(0, loc));
		QByteArray value = QByteArray::fromStdString(h.substr(loc+2));
		request.setRawHeader(name, value);
	}


	if (request_type == "POST") {
		QByteArray postDataBA;

		if (postData && postDataSize)
			postDataBA = QByteArray(postData, postDataSize);
		else if (postData)
			postDataBA = QString(postData).toUtf8();

		reply = qnam.post(request, postDataBA);
	} else
		reply = qnam.get(request);

	QObject::connect(reply, &QNetworkReply::finished, [&]() {
		loop.quit();
	});

	QObject::connect(reply, &QIODevice::readyRead, [&]() {
		buffer.append(reply->readAll());
	});

	QObject::connect(reply, &QNetworkReply::errorOccurred,
			 [&](QNetworkReply::NetworkError code) {
		in_error = true;

		errorString = reply->errorString();
		blog(LOG_WARNING,
		     "RemoteText: HTTP request failed. %s",
		     errorString.toStdString().c_str());

		loop.quit();
	});

	loop.exec();

	str = buffer.toStdString();
	QByteArray sigHeaderLabel = QString("X-Signature").toUtf8();
	if (signature && reply->hasRawHeader(sigHeaderLabel)) {
		QString sigValue = reply->rawHeader(sigHeaderLabel);
		*signature = sigValue.toStdString();
	}

	if (responseCode)
		*responseCode = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute
				).toInt();

	reply->deleteLater();

	if (in_error) {
		error = errorString.toStdString();
		if (fail_on_error)
			return false;
	}

	return true;
}
