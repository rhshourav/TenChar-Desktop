#include "message_model.hpp"

QVariant MessageModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& m = items_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return QVariant::fromValue<qint64>(m.id);
        case TextRole: return QString::fromStdString(m.text);
        case MineRole: return m.mine;
        case DeliveryRole:
            return m.delivery == tc::Delivery::Queued ? "queued" : m.delivery == tc::Delivery::Sent ? "sent" : "none";
    }
    return {};
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    return {{IdRole, "msgId"}, {TextRole, "text"}, {MineRole, "mine"}, {DeliveryRole, "delivery"}};
}

void MessageModel::update(const std::vector<tc::ChatMessage>& next) {
    const std::size_t old = items_.size();
    bool prefix = next.size() >= old;
    for (std::size_t i = 0; prefix && i < old; ++i) prefix = items_[i].id == next[i].id && items_[i].mine == next[i].mine && items_[i].text == next[i].text;
    if (!prefix) {
        beginResetModel();
        items_ = next;
        endResetModel();
        return;
    }
    for (std::size_t i = 0; i < old; ++i)
        if (items_[i].delivery != next[i].delivery) {
            items_[i].delivery = next[i].delivery;
            emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {DeliveryRole});
        }
    if (next.size() > old) {
        beginInsertRows({}, static_cast<int>(old), static_cast<int>(next.size()) - 1);
        items_.insert(items_.end(), next.begin() + static_cast<std::ptrdiff_t>(old), next.end());
        endInsertRows();
    }
}
