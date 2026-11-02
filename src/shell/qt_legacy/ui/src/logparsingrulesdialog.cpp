/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "logparsingrulesdialog.h"
#include "apievents.h"
#include "event_mapping.h"
#include "eventvisualization.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

//
// LogParsingRulesDialog
//

LogParsingRulesDialog::LogParsingRulesDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
    loadRules();
    updateRuleList();
}

void LogParsingRulesDialog::setupUI()
{
    setWindowTitle(tr("Log Parsing Rules"));
    setMinimumSize(800, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // Labels
    auto* infoLabel = new QLabel(tr(
        "Configure log parsing rules to extract timestamp, PID, TID, module, and log level from log lines.\n"
        "Use named capture groups in the pattern: (?<timestamp>...), (?<pid>...), (?<tid>...), (?<module>...), (?<log_level>...)"
    ), this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    // Rule list
    ruleTable_ = new QTableWidget(this);
    ruleTable_->setColumnCount(5);
    ruleTable_->setHorizontalHeaderLabels({
        tr("Name"), tr("File Pattern"), tr("Default"), tr("Enabled"), tr("Pattern Preview")
    });
    ruleTable_->horizontalHeader()->setStretchLastSection(true);
    ruleTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    ruleTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    ruleTable_->verticalHeader()->setVisible(false);
    mainLayout->addWidget(ruleTable_);

    // Button row
    auto* buttonLayout = new QHBoxLayout();
    
    addButton_ = new QPushButton(tr("Add"), this);
    editButton_ = new QPushButton(tr("Edit"), this);
    deleteButton_ = new QPushButton(tr("Delete"), this);
    setDefaultButton_ = new QPushButton(tr("Set as Default"), this);
    
    buttonLayout->addWidget(addButton_);
    buttonLayout->addWidget(editButton_);
    buttonLayout->addWidget(deleteButton_);
    buttonLayout->addWidget(setDefaultButton_);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);

    // Test area
    auto* testGroup = new QGroupBox(tr("Test Pattern"), this);
    auto* testLayout = new QVBoxLayout(testGroup);
    
    testInput_ = new QTextEdit(this);
    testInput_->setPlaceholderText(tr("Paste a sample log line here to test..."));
    testInput_->setMaximumHeight(80);
    testLayout->addWidget(testInput_);
    
    testButton_ = new QPushButton(tr("Test"), this);
    testLayout->addWidget(testButton_);
    
    testResult_ = new QLabel(this);
    testResult_->setWordWrap(true);
    testResult_->setStyleSheet("QLabel { padding: 5px; background-color: #f0f0f0; }");
    testLayout->addWidget(testResult_);
    
    mainLayout->addWidget(testGroup);

    // OK/Cancel buttons
    auto* dialogButtonLayout = new QHBoxLayout();
    okButton_ = new QPushButton(tr("OK"), this);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    dialogButtonLayout->addStretch();
    dialogButtonLayout->addWidget(okButton_);
    dialogButtonLayout->addWidget(cancelButton_);
    mainLayout->addLayout(dialogButtonLayout);

    // Connect signals
    connect(addButton_, &QPushButton::clicked, this, &LogParsingRulesDialog::onAddRule);
    connect(editButton_, &QPushButton::clicked, this, &LogParsingRulesDialog::onEditRule);
    connect(deleteButton_, &QPushButton::clicked, this, &LogParsingRulesDialog::onDeleteRule);
    connect(setDefaultButton_, &QPushButton::clicked, this, &LogParsingRulesDialog::onSetDefault);
    connect(testButton_, &QPushButton::clicked, this, &LogParsingRulesDialog::onTestPattern);
    connect(okButton_, &QPushButton::clicked, this, &LogParsingRulesDialog::onAccept);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(ruleTable_, &QTableWidget::itemSelectionChanged, this, &LogParsingRulesDialog::updateButtonStates);
    connect(ruleTable_, &QTableWidget::doubleClicked, this, &LogParsingRulesDialog::onEditRule);

    updateButtonStates();
}

void LogParsingRulesDialog::loadRules()
{
    // Parsing rules live embedded in the API log type configs; only configs
    // with an actual parse pattern show up (legacy getLogParsingRules shape).
    // The full list is kept so event-only configs survive the flat replace.
    configs_ = klogg::ui::listLogTypeConfigs();
    for ( size_t i = 0; i < configs_.size(); ++i ) {
        const auto& config = configs_[ i ];
        if ( config.parsingRule.parsePattern.empty() ) {
            continue;
        }
        auto rule = klogg::api::nativeParsingRuleFromApi( config.parsingRule );
        rule.isDefault = config.isDefault;
        rule.enabled = config.enabled;
        rules_.append( rule );
        configIndices_.append( static_cast<int>( i ) );
    }
}

void LogParsingRulesDialog::saveRules()
{
    // Keep the legacy guarantee that one config is the default
    const auto hasDefault = std::any_of( rules_.cbegin(), rules_.cend(),
                                         []( const auto& rule ) { return rule.isDefault; } );
    if ( !hasDefault && !rules_.isEmpty() ) {
        rules_.first().isDefault = true;
    }

    for ( int i = 0; i < rules_.size(); ++i ) {
        const auto& rule = rules_[ i ];
        if ( configIndices_[ i ] < 0 ) {
            klogg::api::LogTypeConfig config{};
            config.name = rule.name.empty() ? std::string{ "Default" } : rule.name;
            config.globPattern
                = rule.fileGlobPattern.empty() ? std::string{ "*" } : rule.fileGlobPattern;
            config.parsingRule = klogg::api::apiParsingRuleFromNative( rule );
            config.isDefault = rule.isDefault;
            config.enabled = rule.enabled;
            configs_.push_back( std::move( config ) );
            continue;
        }

        auto& config = configs_[ static_cast<size_t>( configIndices_[ i ] ) ];
        config.parsingRule = klogg::api::apiParsingRuleFromNative( rule );
        if ( !rule.name.empty() ) {
            config.name = rule.name;
        }
        if ( !rule.fileGlobPattern.empty() ) {
            config.globPattern = rule.fileGlobPattern;
        }
        config.isDefault = rule.isDefault;
        config.enabled = rule.enabled;
    }

    // Drop configs whose row was deleted in this dialog, back to front so the
    // earlier indices stay valid
    std::vector<int> removed( removedConfigIndices_.cbegin(), removedConfigIndices_.cend() );
    std::sort( removed.begin(), removed.end(), []( int left, int right ) { return left > right; } );
    for ( const auto index : removed ) {
        configs_.erase( configs_.begin() + index );
    }

    klogg::ui::replaceLogTypeConfigs( std::move( configs_ ) );
}

void LogParsingRulesDialog::updateRuleList()
{
    ruleTable_->setRowCount( static_cast<int>( rules_.size() ) );

    for (int i = 0; i < rules_.size(); ++i) {
        const auto& rule = rules_[i];
        
        ruleTable_->setItem( i, 0, new QTableWidgetItem( QString::fromStdString( rule.name ) ) );
        ruleTable_->setItem(
            i, 1, new QTableWidgetItem( QString::fromStdString( rule.fileGlobPattern ) ) );
        
        auto* defaultItem = new QTableWidgetItem();
        defaultItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        defaultItem->setCheckState(rule.isDefault ? Qt::Checked : Qt::Unchecked);
        ruleTable_->setItem(i, 2, defaultItem);
        
        auto* enabledItem = new QTableWidgetItem();
        enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        enabledItem->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
        ruleTable_->setItem(i, 3, enabledItem);
        
        // Preview (truncated)
        QString preview = QString::fromStdString( rule.parsePattern );
        if (preview.length() > 50) {
            preview = preview.left(50) + "...";
        }
        ruleTable_->setItem(i, 4, new QTableWidgetItem(preview));
    }
    
    ruleTable_->resizeColumnsToContents();
}

void LogParsingRulesDialog::updateButtonStates()
{
    bool hasSelection = ruleTable_->currentRow() >= 0;
    editButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
    setDefaultButton_->setEnabled(hasSelection);
    testButton_->setEnabled(hasSelection);
}

void LogParsingRulesDialog::onAddRule()
{
    LogParsingRuleEditDialog dialog(this);
    dialog.setWindowTitle(tr("Add Log Parsing Rule"));
    
    if (dialog.exec() == QDialog::Accepted) {
        rules_.append(dialog.getRule());
        configIndices_.append( -1 );
        updateRuleList();
    }
}

void LogParsingRulesDialog::onEditRule()
{
    int row = ruleTable_->currentRow();
    if (row < 0 || row >= rules_.size()) return;
    
    LogParsingRuleEditDialog dialog(this);
    dialog.setWindowTitle(tr("Edit Log Parsing Rule"));
    dialog.setRule(rules_[row]);
    
    if (dialog.exec() == QDialog::Accepted) {
        rules_[row] = dialog.getRule();
        updateRuleList();
    }
}

void LogParsingRulesDialog::onDeleteRule()
{
    int row = ruleTable_->currentRow();
    if (row < 0 || row >= rules_.size()) return;
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Confirm Delete"),
        tr("Are you sure you want to delete this rule?"));
    
    if (reply == QMessageBox::Yes) {
        if ( configIndices_[ row ] >= 0 ) {
            removedConfigIndices_.insert( configIndices_[ row ] );
        }
        rules_.removeAt(row);
        configIndices_.removeAt( row );
        updateRuleList();
        updateButtonStates();
    }
}

void LogParsingRulesDialog::onSetDefault()
{
    int row = ruleTable_->currentRow();
    if (row < 0 || row >= rules_.size()) return;
    
    // Clear default flag from other rules
    for (auto& rule : rules_) {
        rule.isDefault = false;
    }
    
    rules_[row].isDefault = true;
    updateRuleList();
}

void LogParsingRulesDialog::onTestPattern()
{
    int row = ruleTable_->currentRow();
    if (row < 0 || row >= rules_.size()) return;
    
    QString testText = testInput_->toPlainText();
    if (testText.isEmpty()) {
        testResult_->setText(tr("Please enter a test log line."));
        return;
    }
    
    const auto& rule = rules_[row];
    if (rule.parsePattern.empty()) {
        testResult_->setText(tr("Pattern is empty."));
        return;
    }
    
    QRegularExpression regex( QString::fromStdString( rule.parsePattern ) );
    if (!regex.isValid()) {
        testResult_->setText(tr("Invalid pattern: %1").arg(regex.errorString()));
        return;
    }
    
    QRegularExpressionMatch match = regex.match(testText);
    if (!match.hasMatch()) {
        testResult_->setText(tr("No match found."));
        return;
    }
    
    QStringList results;
    QStringList capturedNames = {"timestamp", "pid", "tid", "module", "log_level"};
    
    for (const QString& name : capturedNames) {
        QString value = match.captured(name);
        if (!value.isEmpty()) {
            results.append(QString("%1: %2").arg(name, value));
        }
    }
    
    // Show other named groups
    QStringList allNames = regex.namedCaptureGroups();
    for (const QString& name : allNames) {
        if (name.isEmpty() || capturedNames.contains(name)) continue;
        QString value = match.captured(name);
        if (!value.isEmpty()) {
            results.append(QString("%1: %2").arg(name, value));
        }
    }
    
    if (results.isEmpty()) {
        testResult_->setText(tr("Match found, but no named groups captured."));
    } else {
        testResult_->setText(tr("Match found:\n%1").arg(results.join("\n")));
    }
}

void LogParsingRulesDialog::onAccept()
{
    saveRules();
    accept();
}

//
// LogParsingRuleEditDialog
//

LogParsingRuleEditDialog::LogParsingRuleEditDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
}

void LogParsingRuleEditDialog::setupUI()
{
    setWindowTitle(tr("Edit Log Parsing Rule"));
    setMinimumSize(700, 500);

    auto* mainLayout = new QVBoxLayout(this);

    // Basic info
    auto* infoLayout = new QGridLayout();
    
    int row = 0;
    infoLayout->addWidget(new QLabel(tr("Name:"), this), row, 0);
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("e.g., Standard Log Format"));
    infoLayout->addWidget(nameEdit_, row++, 1);
    
    infoLayout->addWidget(new QLabel(tr("File Pattern (glob):"), this), row, 0);
    fileGlobEdit_ = new QLineEdit(this);
    fileGlobEdit_->setPlaceholderText(tr("e.g., *.log or /var/log/*.log"));
    infoLayout->addWidget(fileGlobEdit_, row++, 1);
    
    mainLayout->addLayout(infoLayout);

    // Regex
    mainLayout->addWidget(new QLabel(tr("Parse Pattern (regex with named groups):"), this));
    patternEdit_ = new QTextEdit(this);
    patternEdit_->setPlaceholderText(tr(
        "Example: (?<timestamp>\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}).*"
        "\\[(?<pid>\\d+)\\].*\\[(?<tid>\\d+)\\].*\\[(?<module>\\w+)\\].*"
        "\\[(?<log_level>\\w+)\\]"
    ));
    patternEdit_->setMaximumHeight(100);
    mainLayout->addWidget(patternEdit_);

    // Timestamp format
    auto* tsFormatLayout = new QHBoxLayout();
    tsFormatLayout->addWidget(new QLabel(tr("Timestamp Format:"), this));
    timestampFormatCombo_ = new QComboBox(this);
    timestampFormatCombo_->addItem(tr("Auto Detect"), "");
    timestampFormatCombo_->addItem(tr("yyyy-MM-dd HH:mm:ss.zzz"), "yyyy-MM-dd HH:mm:ss.zzz");
    timestampFormatCombo_->addItem(tr("yyyy-MM-dd HH:mm:ss"), "yyyy-MM-dd HH:mm:ss");
    timestampFormatCombo_->addItem(tr("yyyy-MM-ddTHH:mm:ss.zzz"), "yyyy-MM-ddTHH:mm:ss.zzz");
    timestampFormatCombo_->addItem(tr("yyyy-MM-ddTHH:mm:ss"), "yyyy-MM-ddTHH:mm:ss");
    timestampFormatCombo_->addItem(tr("HH:mm:ss.zzz"), "HH:mm:ss.zzz");
    timestampFormatCombo_->addItem(tr("HH:mm:ss"), "HH:mm:ss");
    timestampFormatCombo_->addItem(tr("Unix Timestamp (seconds)"), "unix_s");
    timestampFormatCombo_->addItem(tr("Unix Timestamp (milliseconds)"), "unix_ms");
    timestampFormatCombo_->addItem(tr("Custom..."), "custom");
    tsFormatLayout->addWidget(timestampFormatCombo_);
    
    customTimestampFormat_ = new QLineEdit(this);
    customTimestampFormat_->setPlaceholderText(tr("e.g., yyyy/MM/dd HH:mm:ss"));
    customTimestampFormat_->setEnabled(false);
    tsFormatLayout->addWidget(customTimestampFormat_);
    tsFormatLayout->addStretch();
    mainLayout->addLayout(tsFormatLayout);

    // Connect timestamp format
    connect(timestampFormatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                bool isCustom = timestampFormatCombo_->itemData(index).toString() == "custom";
                customTimestampFormat_->setEnabled(isCustom);
            });

    // Options
    auto* optionsLayout = new QHBoxLayout();
    defaultCheck_ = new QCheckBox(tr("Set as Default"), this);
    enabledCheck_ = new QCheckBox(tr("Enabled"), this);
    enabledCheck_->setChecked(true);
    optionsLayout->addWidget(defaultCheck_);
    optionsLayout->addWidget(enabledCheck_);
    optionsLayout->addStretch();
    mainLayout->addLayout(optionsLayout);

    // Test area
    auto* testGroup = new QGroupBox(tr("Test"), this);
    auto* testLayout = new QVBoxLayout(testGroup);
    
    testInput_ = new QTextEdit(this);
    testInput_->setPlaceholderText(tr("Paste a sample log line here..."));
    testInput_->setMaximumHeight(80);
    testLayout->addWidget(testInput_);
    
    testButton_ = new QPushButton(tr("Test Pattern"), this);
    testLayout->addWidget(testButton_);
    
    testResult_ = new QLabel(this);
    testResult_->setWordWrap(true);
    testResult_->setStyleSheet("QLabel { padding: 5px; background-color: #f0f0f0; }");
    testLayout->addWidget(testResult_);
    
    mainLayout->addWidget(testGroup);

    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    okButton_ = new QPushButton(tr("OK"), this);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    buttonLayout->addWidget(okButton_);
    buttonLayout->addWidget(cancelButton_);
    mainLayout->addLayout(buttonLayout);

    // Connect signals
    connect(testButton_, &QPushButton::clicked, this, &LogParsingRuleEditDialog::onTestPattern);
    connect(okButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
}

void LogParsingRuleEditDialog::setRule(const LogParsingRule& rule)
{
    nameEdit_->setText( QString::fromStdString( rule.name ) );
    fileGlobEdit_->setText( QString::fromStdString( rule.fileGlobPattern ) );
    patternEdit_->setPlainText( QString::fromStdString( rule.parsePattern ) );
    defaultCheck_->setChecked(rule.isDefault);
    enabledCheck_->setChecked(rule.enabled);
    
    // Set timestamp format
    if (rule.timestampFormat.empty()) {
        timestampFormatCombo_->setCurrentIndex(0); // Auto Detect
        customTimestampFormat_->clear();
    } else {
        int index = timestampFormatCombo_->findData( QString::fromStdString( rule.timestampFormat ) );
        if (index >= 0) {
            timestampFormatCombo_->setCurrentIndex(index);
        } else {
            // Custom format
            timestampFormatCombo_->setCurrentIndex(timestampFormatCombo_->count() - 1);
            customTimestampFormat_->setText( QString::fromStdString( rule.timestampFormat ) );
        }
    }
}

LogParsingRule LogParsingRuleEditDialog::getRule() const
{
    LogParsingRule rule;
    rule.name = nameEdit_->text().toStdString();
    rule.fileGlobPattern = fileGlobEdit_->text().toStdString();
    rule.parsePattern = patternEdit_->toPlainText().toStdString();
    rule.isDefault = defaultCheck_->isChecked();
    rule.enabled = enabledCheck_->isChecked();
    
    // Get timestamp format
    QString format = timestampFormatCombo_->currentData().toString();
    if (format == "custom") {
        rule.timestampFormat = customTimestampFormat_->text().toStdString();
    } else if (format == "unix_s") {
        rule.timestampFormat = "unix_s";
    } else if (format == "unix_ms") {
        rule.timestampFormat = "unix_ms";
    } else {
        rule.timestampFormat = format.toStdString();
    }
    
    return rule;
}

void LogParsingRuleEditDialog::onTestPattern()
{
    QString testText = testInput_->toPlainText();
    if (testText.isEmpty()) {
        testResult_->setText(tr("Please enter a test log line."));
        return;
    }
    
    QString pattern = patternEdit_->toPlainText();
    if (pattern.isEmpty()) {
        testResult_->setText(tr("Pattern is empty."));
        return;
    }
    
    QRegularExpression regex(pattern);
    if (!regex.isValid()) {
        testResult_->setText(tr("Invalid pattern: %1").arg(regex.errorString()));
        return;
    }
    
    QRegularExpressionMatch match = regex.match(testText);
    if (!match.hasMatch()) {
        testResult_->setText(tr("No match found."));
        return;
    }
    
    QStringList results;
    QStringList capturedNames = {"timestamp", "pid", "tid", "module", "log_level"};
    
    for (const QString& name : capturedNames) {
        QString value = match.captured(name);
        if (!value.isEmpty()) {
            results.append(QString("%1: %2").arg(name, value));
        }
    }
    
    // Show other named groups
    QStringList allNames = regex.namedCaptureGroups();
    for (const QString& name : allNames) {
        if (name.isEmpty() || capturedNames.contains(name)) continue;
        QString value = match.captured(name);
        if (!value.isEmpty()) {
            results.append(QString("%1: %2").arg(name, value));
        }
    }
    
    if (results.isEmpty()) {
        testResult_->setText(tr("Match found, but no named groups captured."));
    } else {
        testResult_->setText(tr("Match found:\n%1").arg(results.join("\n")));
    }
}
