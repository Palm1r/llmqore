// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("LLMCore Chat Example");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);

    engine.loadFromModule("example.LLMCoreChat", "Main");

    return app.exec();
}
