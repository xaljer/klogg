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

#ifndef EVENTCONFIGDIALOG_H
#define EVENTCONFIGDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QGroupBox>

#include "eventvisualization.h"

// Dialog mode - fixed at construction, never changes
enum class EventDialogMode {
    Instance,      // Create/edit Instance event
    SliceBegin,    // Create/edit Slice begin pattern (new event name allowed)
    SliceEnd,      // Add Slice end pattern (must select existing event)
    EditSlice      // Edit existing Slice (both patterns editable)
};

class EventConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit EventConfigDialog(EventDialogMode mode, QWidget* parent = nullptr);
    ~EventConfigDialog() = default;

    void setEventRule(const EventRule& rule);
    void setOriginalText(const QString& text);
    void setInitialPattern(const QString& pattern);
    EventRule getEventRule() const;

private Q_SLOTS:
    void onPatternChanged();
    void onNameChanged(const QString& name);
    void onAccept();

private:
    void setupUI();
    void populateExistingNames();
    void updateParameterTable();
    void updateSliceParameterTable();
    void updateStatusLabel();
    void loadExistingRule(const QString& name);
    bool isExistingEventName(const QString& name) const;

    EventDialogMode mode_;
    
    // Basic info
    QComboBox* nameCombo_;
    QTextEdit* descriptionEdit_;
    QComboBox* categoryCombo_;
    QCheckBox* enabledCheck_;
    
    // Event type selector
    QComboBox* eventTypeCombo_;
    
    // Instance pattern
    QLineEdit* instancePatternEdit_;
    QTableWidget* instanceParamTable_;
    
    // Slice patterns
    QLineEdit* beginPatternEdit_;
    QLineEdit* endPatternEdit_;
    QTableWidget* sliceParamTable_;
    QLineEdit* asyncIdParamEdit_;
    
    // Status
    QLabel* statusLabel_;
    
    QPushButton* okButton_;
    
    EventRule currentRule_;
    QString originalText_;
};

#endif