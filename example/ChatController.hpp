// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QQmlEngine>

#include <LLMCore/Core>

#include "MessageModel.hpp"
#include <QtQmlIntegration>

class ChatController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(MessageModel *messages READ messages CONSTANT)
    Q_PROPERTY(QStringList modelList READ modelList NOTIFY modelListChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool loadingModels READ loadingModels NOTIFY loadingModelsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList toolNames READ toolNames NOTIFY toolNamesChanged)

public:
    explicit ChatController(QObject *parent = nullptr);

    MessageModel *messages() { return &m_messages; }
    QStringList modelList() const { return m_modelList; }
    bool busy() const { return m_busy; }
    bool loadingModels() const { return m_loadingModels; }
    QString status() const { return m_status; }
    QStringList toolNames() const { return m_toolNames; }

    Q_INVOKABLE void setupProvider(
        const QString &provider, const QString &url, const QString &apiKey);
    Q_INVOKABLE void send(const QString &text, const QString &model);
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE void clearChat();

signals:
    void modelListChanged();
    void busyChanged();
    void loadingModelsChanged();
    void statusChanged();
    void toolNamesChanged();

private:
    void createClient(const QString &provider, const QString &url, const QString &apiKey);
    void fetchModels();
    void registerTools();
    void setBusy(bool busy);
    void setLoadingModels(bool loading);
    void setStatus(const QString &status);

    MessageModel m_messages;
    LLMCore::BaseClient *m_client = nullptr;
    QJsonArray m_history;
    QStringList m_modelList;
    bool m_busy = false;
    bool m_loadingModels = false;
    QString m_status = "Select a provider to start.";
    QStringList m_toolNames;
    QString m_currentProvider;
    LLMCore::RequestID m_currentRequest;
};
