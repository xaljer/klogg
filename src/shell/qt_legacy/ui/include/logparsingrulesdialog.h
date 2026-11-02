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

#ifndef LOGPARSINGRULESDIALOG_H
#define LOGPARSINGRULESDIALOG_H

#include <QDialog>
#include <QSet>
#include <QTableWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>

#include <vector>

#include "eventvisualization.h"
#include "klogg/api_events.h"

class LogParsingRulesDialog : public QDialog {
    Q_OBJECT

public:
    explicit LogParsingRulesDialog(QWidget* parent = nullptr);
    ~LogParsingRulesDialog() = default;

private Q_SLOTS:
    void onAddRule();
    void onEditRule();
    void onDeleteRule();
    void onSetDefault();
    void onTestPattern();
    void onAccept();

private:
    void setupUI();
    void loadRules();
    void saveRules();
    void updateRuleList();
    void updateButtonStates();

    QTableWidget* ruleTable_;
    QPushButton* addButton_;
    QPushButton* editButton_;
    QPushButton* deleteButton_;
    QPushButton* setDefaultButton_;
    QPushButton* testButton_;
    QPushButton* okButton_;
    QPushButton* cancelButton_;
    
    QTextEdit* testInput_;
    QLabel* testResult_;
    
    QVector<LogParsingRule> rules_;
    // Full config list at dialog open, so event-only configs survive the flat
    // replace on save; configIndices_ maps each row to its index there
    // (-1 = row added in this dialog)
    std::vector<klogg::api::LogTypeConfig> configs_;
    QVector<int> configIndices_;
    QSet<int> removedConfigIndices_;
};

// Dialog for editing a single log parsing rule
class LogParsingRuleEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit LogParsingRuleEditDialog(QWidget* parent = nullptr);
    ~LogParsingRuleEditDialog() = default;

    void setRule(const LogParsingRule& rule);
    LogParsingRule getRule() const;

private Q_SLOTS:
    void onTestPattern();

private:
    void setupUI();

    QLineEdit* nameEdit_;
    QLineEdit* fileGlobEdit_;
    QTextEdit* patternEdit_;
    QComboBox* timestampFormatCombo_;
    QLineEdit* customTimestampFormat_;
    QCheckBox* defaultCheck_;
    QCheckBox* enabledCheck_;
    
    QTextEdit* testInput_;
    QLabel* testResult_;
    
    QPushButton* testButton_;
    QPushButton* okButton_;
    QPushButton* cancelButton_;
};

#endif
