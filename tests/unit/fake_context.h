#pragma once
#include <nekotune/sdk/extension_context.h>
#include <QDir>
#include <QHash>
#include <QList>
#include <map>
#include <optional>

/// Synchronous in-memory extension context for plugin tests.
class FakeContext final : public nekotune::sdk::ExtensionContext {
  public:
    using Params = nekotune::sdk::Params;
    using Reply = nekotune::sdk::Reply;
    using Done = nekotune::sdk::Done;
    struct Event {
        QString name;
        Params data;
    };
    QString extensionId = "test.extension";
    QString dataPath = QDir::tempPath();
    Params configuration;
    QHash<QString, QString> storedSecrets;
    QHash<QString, QVariant> storedValues;
    std::function<nekotune::Result<void>(const QString &key, const QString &value)> secretWriter;
    std::function<void(const QString &, const Params &, Reply)> hostHandler =
        [](const QString &method, const Params &params, Reply done) {
            done(Params{{"method", method}, {"params", params}, {"task", "fixture-task"}});
        };
    QHash<QString, nekotune::sdk::Handler> serviceHandlers, commandHandlers;
    QHash<QString, nekotune::sdk::IMusicProvider *> musicProviders;
    QHash<QString, nekotune::sdk::ILyricsProvider *> lyricsProviders;
    /// "music/<id>" → {name, download, page}; "lyrics/<id>" → {name}; "commands/<id>" → {title, shortcut}.
    QHash<QString, Params> descriptors;
    std::multimap<QString, std::pair<int, nekotune::sdk::EventHandler>> listeners;
    QList<Event> publishedEvents;
    QStringList logs;
    QList<std::pair<QString, nekotune::sdk::TaskProgress>> reportedTasks;

    /// Deliver a host event to subscribers, like the runtime does.
    void deliver(const QString &name, const Params &event) {
        auto copy = listeners;
        for (const auto &[key, value] : copy)
            if (key == name || key == "*")
                value.second(event);
    }
    bool published(const QString &name) const {
        for (const auto &event : publishedEvents)
            if (event.name == name)
                return true;
        return false;
    }
    nekotune::Result<Params> call(const QString &service, const Params &params = {}) {
        std::optional<nekotune::Result<Params>> result;
        const auto handler = serviceHandlers.value(service);
        if (!handler)
            return nekotune::failure("missing service " + service);
        handler(params, [&](nekotune::Result<Params> value) { result = std::move(value); });
        return result ? *result : nekotune::Result<Params>(nekotune::failure("pending"));
    }

    const nekotune::sdk::ExtensionInfo &info() const override {
        m_info = {extensionId, "1.0.0", dataPath, dataPath};
        return m_info;
    }
    nekotune::sdk::Host &host() override { return m_host; }
    nekotune::sdk::Events &events() override { return m_events; }
    nekotune::sdk::Services &services() override { return m_services; }
    nekotune::sdk::Commands &commands() override { return m_commands; }
    nekotune::sdk::Ui &ui() override { return m_ui; }
    nekotune::sdk::Music &music() override { return m_music; }
    nekotune::sdk::Lyrics &lyrics() override { return m_lyrics; }
    nekotune::sdk::Shuffle &shuffle() override { return m_shuffle; }
    nekotune::sdk::Pcm &pcm() override { return m_pcm; }
    nekotune::sdk::Config &config() override { return m_config; }
    nekotune::sdk::Storage &storage() override { return m_storage; }
    nekotune::sdk::Secrets &secrets() override { return m_secrets; }
    nekotune::sdk::Tasks &tasks() override { return m_tasks; }
    nekotune::sdk::Logger &log() override { return m_logger; }
    std::unique_ptr<nekotune::sdk::ThreadAttribution> attributeThread() override {
        return std::make_unique<nekotune::sdk::ThreadAttribution>();
    }

  private:
    class Handle final : public nekotune::sdk::Registration {
      public:
        explicit Handle(std::function<void()> remove) : m_remove(std::move(remove)) {}
        ~Handle() override { dispose(); }
        void dispose() override {
            if (auto remove = std::exchange(m_remove, {}))
                remove();
        }

      private:
        std::function<void()> m_remove;
    };
    static nekotune::sdk::RegistrationPtr handle(std::function<void()> remove) {
        return std::make_unique<Handle>(std::move(remove));
    }

    struct HostFacet final : nekotune::sdk::Host {
        FakeContext &c;
        explicit HostFacet(FakeContext &c) : c(c) {}
        void call(const QString &method, const Params &params, Reply done) override { c.hostHandler(method, params, std::move(done)); }
    };
    struct EventsFacet final : nekotune::sdk::Events {
        FakeContext &c;
        explicit EventsFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr on(const QString &event, nekotune::sdk::EventHandler handler) override {
            const int key = ++c.m_next;
            c.listeners.insert({event, {key, std::move(handler)}});
            return handle([&c = c, key] {
                std::erase_if(c.listeners, [key](const auto &entry) { return entry.second.first == key; });
            });
        }
        void publish(const QString &event, const Params &data) override { c.publishedEvents.append({event, data}); }
    };
    struct ServicesFacet final : nekotune::sdk::Services {
        FakeContext &c;
        explicit ServicesFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr add(const QString &id, nekotune::sdk::Handler handler) override {
            c.serviceHandlers.insert(id, std::move(handler));
            return handle([&c = c, id] { c.serviceHandlers.remove(id); });
        }
        void call(const QString &, const QString &, const Params &, Reply done) override {
            done(nekotune::failure("unsupported"));
        }
    };
    struct CommandsFacet final : nekotune::sdk::Commands {
        FakeContext &c;
        explicit CommandsFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr add(const QString &id, nekotune::sdk::Handler handler,
                                           const nekotune::sdk::CommandInfo &info) override {
            c.commandHandlers.insert(id, std::move(handler));
            c.descriptors.insert("commands/" + id, {{"title", info.title}, {"shortcut", info.shortcut}});
            return handle([&c = c, id] { c.commandHandlers.remove(id); });
        }
    };
    struct UiFacet final : nekotune::sdk::Ui {
        nekotune::sdk::RegistrationPtr add(const QString &, const QString &, const Params &) override {
            return handle([] {});
        }
    };
    struct MusicFacet final : nekotune::sdk::Music {
        FakeContext &c;
        explicit MusicFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr add(const QString &id, nekotune::sdk::IMusicProvider *provider,
                                           const nekotune::sdk::MusicSourceInfo &info) override {
            if (c.musicProviders.contains(id))
                qFatal("Duplicate music registration");
            c.musicProviders.insert(id, provider);
            c.descriptors.insert("music/" + id, {{"name", info.name}, {"download", info.download}, {"page", info.page}});
            return handle([&c = c, id] { c.musicProviders.remove(id); });
        }
    };
    struct LyricsFacet final : nekotune::sdk::Lyrics {
        FakeContext &c;
        explicit LyricsFacet(FakeContext &c) : c(c) {}
        nekotune::sdk::RegistrationPtr add(const QString &id, nekotune::sdk::ILyricsProvider *provider,
                                           const nekotune::sdk::LyricsSourceInfo &info) override {
            if (c.lyricsProviders.contains(id))
                qFatal("Duplicate lyrics registration");
            c.lyricsProviders.insert(id, provider);
            c.descriptors.insert("lyrics/" + id, {{"name", info.name}});
            return handle([&c = c, id] { c.lyricsProviders.remove(id); });
        }
    };
    struct ShuffleFacet final : nekotune::sdk::Shuffle {
        nekotune::sdk::RegistrationPtr add(const QString &, nekotune::sdk::IShuffleProvider *,
                                           const nekotune::sdk::ShuffleInfo &) override {
            return {};
        }
    };
    struct PcmFacet final : nekotune::sdk::Pcm {
        nekotune::sdk::PcmCapabilities capabilities() const override { return {false, {}, "pcm_unavailable"}; }
        nekotune::sdk::RegistrationPtr subscribe(nekotune::sdk::PcmHandler) override { return {}; }
    };
    struct ConfigFacet final : nekotune::sdk::Config {
        FakeContext &c;
        explicit ConfigFacet(FakeContext &c) : c(c) {}
        Params get() const override { return c.configuration; }
        void update(const Params &config, Done done) override {
            c.configuration = config;
            c.deliver("extension.config_changed", {{"event", "extension.config_changed"}, {"config", config}});
            done({});
        }
    };
    struct StorageFacet final : nekotune::sdk::Storage {
        FakeContext &c;
        explicit StorageFacet(FakeContext &c) : c(c) {}
        void get(const QString &key, std::function<void(nekotune::Result<QVariant>)> done) override {
            done(c.storedValues.value(key));
        }
        void set(const QString &key, const QVariant &value, Done done) override {
            c.storedValues.insert(key, value);
            done({});
        }
    };
    struct SecretsFacet final : nekotune::sdk::Secrets {
        FakeContext &c;
        explicit SecretsFacet(FakeContext &c) : c(c) {}
        void get(const QString &key, std::function<void(nekotune::Result<QString>)> done) override {
            done(c.storedSecrets.value(key));
        }
        void set(const QString &key, const QString &value, Done done) override {
            if (c.secretWriter)
                if (auto written = c.secretWriter(key, value); !written)
                    return done(written);
            c.storedSecrets.insert(key, value);
            done({});
        }
        void remove(const QString &key, Done done) override {
            c.storedSecrets.remove(key);
            done({});
        }
    };
    struct TasksFacet final : nekotune::sdk::Tasks {
        FakeContext &c;
        explicit TasksFacet(FakeContext &c) : c(c) {}
        void report(const QString &task, const nekotune::sdk::TaskProgress &progress) override {
            c.reportedTasks.append({task, progress});
        }
    };
    struct LoggerFacet final : nekotune::sdk::Logger {
        FakeContext &c;
        explicit LoggerFacet(FakeContext &c) : c(c) {}
        void write(QtMsgType, const QString &message) override { c.logs.append(message); }
    };

    mutable nekotune::sdk::ExtensionInfo m_info;
    int m_next = 0;
    HostFacet m_host{*this};
    EventsFacet m_events{*this};
    ServicesFacet m_services{*this};
    CommandsFacet m_commands{*this};
    UiFacet m_ui;
    MusicFacet m_music{*this};
    LyricsFacet m_lyrics{*this};
    ShuffleFacet m_shuffle;
    PcmFacet m_pcm;
    ConfigFacet m_config{*this};
    StorageFacet m_storage{*this};
    SecretsFacet m_secrets{*this};
    TasksFacet m_tasks{*this};
    LoggerFacet m_logger{*this};
};
