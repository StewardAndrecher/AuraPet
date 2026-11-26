#include "role_card_editor.h"
#include "role_package.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QStringList textValues(const QJsonValue& value)
{
    QStringList lines;
    for (const auto& part : value.toArray()) {
        lines.append(part.toString());
    }
    return lines;
}

QString exampleLine(const QJsonObject& example, const QString& speaker)
{
    for (const auto& value : example.value(QStringLiteral("messages")).toArray()) {
        const auto message = value.toObject();
        if (message.value(QStringLiteral("role")).toString() == speaker) {
            return message.value(QStringLiteral("content")).toString();
        }
    }
    return {};
}

bool editExample(QWidget& parent, QJsonObject& example)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("填写对话示例"));
    dialog.resize(640, 485);
    auto* form = new QFormLayout(&dialog);
    auto* question = new QPlainTextEdit(exampleLine(example, QStringLiteral("user")), &dialog);
    auto* answer = new QPlainTextEdit(exampleLine(example, QStringLiteral("assistant")), &dialog);
    auto* tags = new QLineEdit(textValues(example.value(QStringLiteral("tags"))).join(QStringLiteral("，")), &dialog);
    tags->setPlaceholderText(QStringLiteral("可选，多个词用逗号分隔，例如作息，熬夜"));
    form->addRow(QStringLiteral("用户发言"), question);
    form->addRow(QStringLiteral("角色回答"), answer);
    form->addRow(QStringLiteral("匹配关键词"), tags);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* confirm = buttons->addButton(QStringLiteral("确定"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    form->addRow(buttons);
    const auto update = [&] {
        confirm->setEnabled(!question->toPlainText().trimmed().isEmpty() && !answer->toPlainText().trimmed().isEmpty());
    };
    QObject::connect(question, &QPlainTextEdit::textChanged, &dialog, update);
    QObject::connect(answer, &QPlainTextEdit::textChanged, &dialog, update);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    auto messages = example.value(QStringLiteral("messages")).toArray();
    for (const auto& [speaker, content] : {qMakePair(QStringLiteral("user"), question->toPlainText().trimmed()),
                                          qMakePair(QStringLiteral("assistant"), answer->toPlainText().trimmed())}) {
        qsizetype index = 0;
        while (index < messages.size() && messages[index].toObject().value(QStringLiteral("role")).toString() != speaker) {
            ++index;
        }
        if (index == messages.size()) {
            messages.append(QJsonObject{{QStringLiteral("role"), speaker}, {QStringLiteral("content"), content}});
        } else {
            auto message = messages[index].toObject();
            message.insert(QStringLiteral("content"), content);
            messages.replace(index, message);
        }
    }
    if (!example.contains(QStringLiteral("id"))) {
        example.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    const auto words = tags->text().trimmed().split(QRegularExpression(QStringLiteral("[,，、]+")), Qt::SkipEmptyParts);
    QJsonArray keywords;
    for (const auto& word : words) {
        if (!word.trimmed().isEmpty()) {
            keywords.append(word.trimmed());
        }
    }
    example.insert(QStringLiteral("tags"), keywords);
    example.insert(QStringLiteral("messages"), messages);
    return true;
}
}

QString roleCardText(const QJsonObject& card)
{
    if (card.isEmpty()) {
        return QStringLiteral("这个角色还没有填写角色卡。点击「编辑角色卡」添加身份与立场、AI 聊天角色设定和对话示例。");
    }
    QStringList sections;
    const auto section = [&](const QString& title, const QString& value) {
        sections.append(title + u'\n' + (value.trimmed().isEmpty() ? QStringLiteral("尚未填写") : value));
    };
    section(QStringLiteral("角色卡名称"), card.value(QStringLiteral("displayName")).toString());
    const auto address = card.value(QStringLiteral("userAddress")).toString();
    if (!address.isEmpty()) {
        section(QStringLiteral("对用户的称呼"), address);
    }
    section(QStringLiteral("身份与立场"), card.value(QStringLiteral("identityAndStance")).toString());
    section(QStringLiteral("AI 聊天角色设定"), textValues(card.value(QStringLiteral("corePrompt"))).join(u'\n'));
    QStringList examples;
    for (const auto& example : card.value(QStringLiteral("examples")).toArray()) {
        QStringList lines{QStringLiteral("示例 %1").arg(examples.size() + 1)};
        const auto item = example.toObject();
        const auto tags = textValues(item.value(QStringLiteral("tags")));
        if (!tags.isEmpty()) {
            lines.append(QStringLiteral("关键词  %1").arg(tags.join(QStringLiteral("、"))));
        }
        for (const auto& value : item.value(QStringLiteral("messages")).toArray()) {
            const auto message = value.toObject();
            lines.append((message.value(QStringLiteral("role")).toString() == QStringLiteral("user")
                              ? QStringLiteral("用户  ") : QStringLiteral("角色  "))
                         + message.value(QStringLiteral("content")).toString());
        }
        examples.append(lines.join(u'\n'));
    }
    section(QStringLiteral("对话示例"), examples.join(QStringLiteral("\n\n")));
    const auto guidance = card.value(QStringLiteral("exampleGuidance")).toString();
    if (!guidance.isEmpty()) {
        section(QStringLiteral("示例说明"), guidance);
    }
    QStringList backgrounds;
    for (const auto& block : card.value(QStringLiteral("contextBlocks")).toArray()) {
        const auto item = block.toObject();
        const auto tags = textValues(item.value(QStringLiteral("tags"))).join(QStringLiteral("、"));
        const auto detail = item.value(QStringLiteral("text")).toString();
        backgrounds.append(tags.isEmpty() ? detail : QStringLiteral("%1\n%2").arg(tags, detail));
    }
    if (!backgrounds.isEmpty()) {
        section(QStringLiteral("背景资料"), backgrounds.join(QStringLiteral("\n\n")));
    }
    const auto voice = card.value(QStringLiteral("voice")).toObject();
    QStringList voiceNotes;
    for (const auto& [key, caption] : {qMakePair(QStringLiteral("zh"), QStringLiteral("中文音色  ")),
                                       qMakePair(QStringLiteral("ja"), QStringLiteral("日文音色  ")),
                                       qMakePair(QStringLiteral("streamingPrompt"), QStringLiteral("朗读提示  "))}) {
        if (voice.value(key).isString()) {
            voiceNotes.append(caption + voice.value(key).toString());
        }
    }
    if (!voiceNotes.isEmpty()) {
        section(QStringLiteral("音色设定"), voiceNotes.join(u'\n'));
    }
    return sections.join(QStringLiteral("\n\n"));
}

bool editRoleCard(QWidget& parent, QJsonObject& card, const QString& roleId, const QString& roleName)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("编辑角色卡"));
    dialog.resize(740, 675);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs, 1);
    auto* profile = new QWidget(tabs);
    auto* form = new QFormLayout(profile);
    auto* name = new QLineEdit(card.value(QStringLiteral("displayName")).toString(roleName), profile);
    name->setMaxLength(80);
    name->setObjectName(QStringLiteral("roleCardName"));
    auto* address = new QLineEdit(card.value(QStringLiteral("userAddress")).toString(), profile);
    address->setMaxLength(80);
    address->setPlaceholderText(QStringLiteral("可选，例如博士"));
    auto* identity = new QPlainTextEdit(card.value(QStringLiteral("identityAndStance")).toString(), profile);
    identity->setObjectName(QStringLiteral("roleCardIdentity"));
    identity->setPlaceholderText(QStringLiteral("角色是谁，来自哪里，有什么目标，如何看待用户？"));
    auto* prompt = new QPlainTextEdit(textValues(card.value(QStringLiteral("corePrompt"))).join(u'\n'), profile);
    prompt->setObjectName(QStringLiteral("roleCardPrompt"));
    prompt->setPlaceholderText(QStringLiteral("写下 AI 扮演角色时的性格、说话方式、行为与表达边界。"));
    auto* length = new QLabel(profile);
    const auto updateLength = [&] {
        length->setText(QStringLiteral("身份与立场 %1 / 4000     AI 聊天角色设定 %2 / 4000")
                            .arg(identity->toPlainText().trimmed().size()).arg(prompt->toPlainText().trimmed().size()));
    };
    form->addRow(QStringLiteral("角色卡名称"), name);
    form->addRow(QStringLiteral("对用户的称呼"), address);
    form->addRow(QStringLiteral("身份与立场"), identity);
    form->addRow(QStringLiteral("AI 聊天角色设定"), prompt);
    form->addRow(length);
    QObject::connect(identity, &QPlainTextEdit::textChanged, &dialog, updateLength);
    QObject::connect(prompt, &QPlainTextEdit::textChanged, &dialog, updateLength);
    updateLength();
    tabs->addTab(profile, QStringLiteral("身份与 AI 设定"));

    auto* examplesPage = new QWidget(tabs);
    auto* exampleLayout = new QVBoxLayout(examplesPage);
    auto* examples = new QTableWidget(examplesPage);
    examples->setObjectName(QStringLiteral("roleCardExamples"));
    examples->setColumnCount(3);
    examples->setHorizontalHeaderLabels({QStringLiteral("用户发言"), QStringLiteral("角色回答"), QStringLiteral("关键词")});
    examples->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    examples->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    examples->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    examples->setSelectionBehavior(QAbstractItemView::SelectRows);
    examples->setSelectionMode(QAbstractItemView::SingleSelection);
    examples->setEditTriggers(QAbstractItemView::NoEditTriggers);
    exampleLayout->addWidget(examples, 1);
    auto* actions = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("添加示例…"), examplesPage);
    auto* edit = new QPushButton(QStringLiteral("编辑所选…"), examplesPage);
    auto* remove = new QPushButton(QStringLiteral("移除所选"), examplesPage);
    for (auto* button : {add, edit, remove}) {
        actions->addWidget(button);
    }
    actions->addStretch();
    exampleLayout->addLayout(actions);
    auto* instructions = new QLabel(QStringLiteral("匹配关键词时优先选用相关示例。没有关键词的示例可用作通用示例。"), examplesPage);
    instructions->setWordWrap(true);
    exampleLayout->addWidget(instructions);
    auto rows = card.value(QStringLiteral("examples")).toArray();
    const auto refreshRows = [&](int selection) {
        examples->setRowCount(rows.size());
        for (int index = 0; index < rows.size(); ++index) {
            const auto data = rows[index].toObject();
            const QStringList values{exampleLine(data, QStringLiteral("user")), exampleLine(data, QStringLiteral("assistant")),
                                     textValues(data.value(QStringLiteral("tags"))).join(QStringLiteral("、"))};
            for (int column = 0; column < values.size(); ++column) {
                auto* item = new QTableWidgetItem(values[column].simplified());
                item->setToolTip(values[column]);
                examples->setItem(index, column, item);
            }
        }
        if (selection >= 0 && selection < rows.size()) {
            examples->selectRow(selection);
        }
        edit->setEnabled(examples->currentRow() >= 0);
        remove->setEnabled(edit->isEnabled());
    };
    const auto editRow = [&](int index) {
        if (index < 0) {
            return;
        }
        auto example = rows.at(index).toObject();
        if (editExample(dialog, example)) {
            rows.replace(index, example);
            refreshRows(index);
        }
    };
    QObject::connect(examples, &QTableWidget::currentCellChanged, &dialog, [&](int row) {
        edit->setEnabled(row >= 0);
        remove->setEnabled(row >= 0);
    });
    QObject::connect(examples, &QTableWidget::cellDoubleClicked, &dialog, editRow);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        QJsonObject example;
        if (editExample(dialog, example)) {
            rows.append(example);
            refreshRows(rows.size() - 1);
        }
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] { editRow(examples->currentRow()); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        const int index = examples->currentRow();
        if (index >= 0) {
            rows.removeAt(index);
            refreshRows(qMin(index, static_cast<int>(rows.size()) - 1));
        }
    });
    refreshRows(rows.isEmpty() ? -1 : 0);
    tabs->addTab(examplesPage, QStringLiteral("对话示例"));

    auto* buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(QStringLiteral("保存角色卡"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto updated = card;
        updated.insert(QStringLiteral("schemaVersion"), 1);
        if (!updated.contains(QStringLiteral("id"))) {
            updated.insert(QStringLiteral("id"), roleId);
        }
        updated.insert(QStringLiteral("displayName"), name->text().trimmed());
        updated.insert(QStringLiteral("userAddress"), address->text().trimmed());
        updated.insert(QStringLiteral("identityAndStance"), identity->toPlainText().trimmed());
        QJsonArray core;
        for (const auto& line : prompt->toPlainText().split(u'\n')) {
            if (!line.trimmed().isEmpty()) {
                core.append(line.trimmed());
            }
        }
        updated.insert(QStringLiteral("corePrompt"), core);
        updated.insert(QStringLiteral("examples"), rows);
        QString error;
        if (name->text().trimmed().isEmpty()) {
            error = QStringLiteral("请填写角色卡名称。");
        } else if (prompt->toPlainText().trimmed().size() > 4000) {
            error = QStringLiteral("AI 聊天角色设定最多 4000 字符。");
        } else if (validateRoleCard(updated, error)) {
            card = std::move(updated);
            dialog.accept();
            return;
        }
        QMessageBox::warning(&dialog, QStringLiteral("无法保存角色卡"), error);
    });
    return dialog.exec() == QDialog::Accepted;
}
