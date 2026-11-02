#pragma once
#include <QAbstractListModel>
#include <vector>

#include "tc/session.hpp"

/// The chat list. Appends insert rows (so the list can animate only the new ones); anything else resets.
class MessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TextRole, MineRole, DeliveryRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(items_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void update(const std::vector<tc::ChatMessage>& next);

private:
    std::vector<tc::ChatMessage> items_;
};
