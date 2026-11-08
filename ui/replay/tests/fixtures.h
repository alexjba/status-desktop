#pragma once

#include <StatusReplay/recorder.h>

#include <QAbstractListModel>
#include <QObject>
#include <QVariant>

// Stand-ins for Nim QObjects: same property/signal/slot shapes the Nim views expose.

inline StatusReplay::Recorder* g_recorder = nullptr;

// What a QML consumer bound to these properties does: read them (through qt_metacall).
inline void readLikeQml(QObject* object, const QByteArrayList& names)
{
    for (const auto& name : names)
        object->property(name.constData());
}

// Mirrors the nimqml metacall hook: every metacall is reported after it was handled.
template <typename T>
class Hooked : public T
{
public:
    using T::T;

    int qt_metacall(QMetaObject::Call call, int index, void** argv) override
    {
        const int absolute = index;
        const int result = T::qt_metacall(call, index, argv);
        if (g_recorder)
            g_recorder->metacallObserved(this, call, absolute, argv);
        return result;
    }
};

class Account : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(int balance READ balance NOTIFY balanceChanged)
    Q_PROPERTY(bool mnemonicBackedUp READ mnemonicBackedUp CONSTANT)
    Q_PROPERTY(QString mnemonic READ mnemonic CONSTANT)

public:
    using QObject::QObject;

    QString name() const { return m_name; }
    void setName(const QString& name)
    {
        m_name = name;
        emit nameChanged();
    }

    int balance() const { return m_balance; }
    bool mnemonicBackedUp() const { return true; }
    QString mnemonic() const { return QStringLiteral("apple banana cherry"); }
    // Like Nim views whose slot tears the object down (e.g. a module cleanup).
    Q_INVOKABLE void dispose() { delete this; }
    void setBalance(int balance)
    {
        m_balance = balance;
        emit balanceChanged();
    }


signals:
    void nameChanged();
    void balanceChanged();

private:
    QString m_name;
    int m_balance = 0;
};

class AccountsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        BalanceRole,
        AccountRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int count() const { return m_rows.size(); }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const auto& row = m_rows[index.row()];
        switch (role) {
        case NameRole:
            return row.name;
        case BalanceRole:
            return row.balance;
        case AccountRole:
            return QVariant::fromValue<QObject*>(row.account);
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return { { NameRole, "name" }, { BalanceRole, "balance" }, { AccountRole, "account" } };
    }

    void insert(int at, const QString& name, int balance, QObject* account = nullptr)
    {
        beginInsertRows({}, at, at);
        m_rows.insert(at, { name, balance, account });
        endInsertRows();
        emit countChanged();
    }

    void remove(int at)
    {
        beginRemoveRows({}, at, at);
        m_rows.removeAt(at);
        endRemoveRows();
        emit countChanged();
    }

    void move(int from, int to)
    {
        beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
        m_rows.move(from, to);
        endMoveRows();
    }

    void setBalance(int at, int balance)
    {
        m_rows[at].balance = balance;
        emit dataChanged(index(at), index(at), { BalanceRole });
    }

    void resetTo(const QStringList& names)
    {
        beginResetModel();
        m_rows.clear();
        for (const auto& n : names)
            m_rows.append({ n, 0, nullptr });
        endResetModel();
        emit countChanged();
    }

    Q_INVOKABLE QString nameAt(int row) const { return m_rows.value(row).name; }


signals:
    void countChanged();

private:
    struct Row
    {
        QString name;
        int balance;
        QObject* account;
    };
    QList<Row> m_rows;
};

class WalletView : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(double rate READ rate CONSTANT)
    Q_PROPERTY(qint64 timestamp READ timestamp CONSTANT)
    Q_PROPERTY(QVariant current READ current NOTIFY currentChanged)
    Q_PROPERTY(QVariant accounts READ accounts CONSTANT)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    // Like a Nim getter that is only safe once QML asks for it.
    Q_PROPERTY(QString fragile READ fragile NOTIFY titleChanged)

public:
    explicit WalletView(QObject* parent = nullptr)
        : QObject(parent)
        , m_accounts(new Hooked<AccountsModel>(this))
    {
    }

    QString title() const { return m_title; }
    void setTitle(const QString& title)
    {
        m_title = title;
        emit titleChanged();
    }
    bool loading() const { return m_loading; }
    void setLoading(bool loading)
    {
        m_loading = loading;
        emit loadingChanged();
    }
    double rate() const { return 1.25; }
    qint64 timestamp() const { return 1'700'000'000'123; }
    QVariant current() const { return QVariant::fromValue<QObject*>(m_current); }
    void setCurrent(QObject* current)
    {
        m_current = current;
        emit currentChanged();
    }
    QVariant accounts() const { return QVariant::fromValue<QObject*>(m_accounts); }
    AccountsModel* accountsModel() const { return m_accounts; }

    QString fragile() const
    {
        ++m_fragileReads;
        return {};
    }
    int fragileReads() const { return m_fragileReads; }

    QString filter() const { return m_filter; }
    void setFilter(const QString& filter)
    {
        m_filter = filter;
        emit filterChanged();
    }

    Q_INVOKABLE QString formatBalance(int amount, const QString& symbol) const
    {
        return QStringLiteral("%1 %2").arg(amount).arg(symbol);
    }
    Q_INVOKABLE bool login(const QString& password) { return password == QStringLiteral("secret"); }
    // Shaped like the onboarding module's loginRequested(keyUid, loginFlow, dataJson).
    Q_INVOKABLE QString loginRequested(const QString& keyUid, int flow, const QString& dataJson)
    {
        return QStringLiteral("%1:%2:%3").arg(keyUid).arg(flow).arg(dataJson.size());
    }
    Q_INVOKABLE QVariant accountAt(int row) const
    {
        return m_accounts->data(m_accounts->index(row), AccountsModel::AccountRole);
    }
    Q_INVOKABLE void refresh() { ++m_refreshes; }

    void notifyTransfer(const QString& txHash, int amount) { emit transferConfirmed(txHash, amount); }


signals:
    void titleChanged();
    void loadingChanged();
    void currentChanged();
    void filterChanged();
    void transferConfirmed(const QString& txHash, int amount);

private:
    QString m_title;
    bool m_loading = false;
    QObject* m_current = nullptr;
    AccountsModel* m_accounts;
    QString m_filter;
    int m_refreshes = 0;
    mutable int m_fragileReads = 0;
};
