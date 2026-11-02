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

#include "eventconfigdialog.h"
#include "apievents.h"
#include "eventvisualization.h"
#include <QDialogButtonBox>
#include <QMessageBox>

#include <algorithm>

EventConfigDialog::EventConfigDialog(EventDialogMode mode, QWidget* parent)
    : QDialog(parent)
    , mode_(mode)
{
    setupUI();
    populateExistingNames();
}

void EventConfigDialog::setupUI()
{
    setWindowTitle(tr("Configure Event Rule"));
    setMinimumSize(550, 600);

    auto* mainLayout = new QVBoxLayout(this);
    
    // Basic info section
    auto* basicGroup = new QGroupBox(tr("Basic Information"), this);
    auto* basicLayout = new QGridLayout(basicGroup);
    
    int row = 0;
    
    // Name
    basicLayout->addWidget(new QLabel(tr("Event Name:"), this), row, 0);
    nameCombo_ = new QComboBox(this);
    nameCombo_->setEditable(mode_ != EventDialogMode::SliceEnd);
    nameCombo_->setInsertPolicy(QComboBox::NoInsert);
    if (mode_ == EventDialogMode::SliceEnd) {
        nameCombo_->setPlaceholderText(tr("Select existing event"));
    } else {
        nameCombo_->lineEdit()->setPlaceholderText(tr("Enter event name"));
    }
    basicLayout->addWidget(nameCombo_, row++, 1);
    
    // Category
    basicLayout->addWidget(new QLabel(tr("Category:"), this), row, 0);
    categoryCombo_ = new QComboBox(this);
    categoryCombo_->setEditable(true);
    categoryCombo_->addItems({"General", "Network", "File", "Process", "Error", "Warning", "Info", "Debug"});
    basicLayout->addWidget(categoryCombo_, row++, 1);
    
    // Event Type
    basicLayout->addWidget(new QLabel(tr("Event Type:"), this), row, 0);
    eventTypeCombo_ = new QComboBox(this);
    eventTypeCombo_->addItem(tr("Instance (Instant Event)"), static_cast<int>(EventType::Instance));
    eventTypeCombo_->addItem(tr("Slice (Duration Event)"), static_cast<int>(EventType::Slice));
    
    // Set event type based on mode
    bool isSliceMode = (mode_ == EventDialogMode::SliceBegin || 
                        mode_ == EventDialogMode::SliceEnd || 
                        mode_ == EventDialogMode::EditSlice);
    eventTypeCombo_->setCurrentIndex(isSliceMode ? 1 : 0);
    eventTypeCombo_->setEnabled(mode_ == EventDialogMode::Instance);
    basicLayout->addWidget(eventTypeCombo_, row++, 1);
    
    // Enabled
    enabledCheck_ = new QCheckBox(tr("Enabled"), this);
    enabledCheck_->setChecked(true);
    basicLayout->addWidget(enabledCheck_, row++, 1);
    
    // Description
    basicLayout->addWidget(new QLabel(tr("Description:"), this), row, 0);
    descriptionEdit_ = new QTextEdit(this);
    descriptionEdit_->setMaximumHeight(60);
    basicLayout->addWidget(descriptionEdit_, row++, 1);
    
    mainLayout->addWidget(basicGroup);
    
    // Instance pattern section
    auto* instanceGroup = new QGroupBox(tr("Pattern"), this);
    auto* instanceLayout = new QVBoxLayout(instanceGroup);
    
    auto* instancePatternRow = new QHBoxLayout();
    instancePatternRow->addWidget(new QLabel(tr("Pattern:"), this));
    instancePatternEdit_ = new QLineEdit(this);
    instancePatternRow->addWidget(instancePatternEdit_);
    instanceLayout->addLayout(instancePatternRow);
    
    instanceParamTable_ = new QTableWidget(0, 2, this);
    instanceParamTable_->setHorizontalHeaderLabels({tr("Parameter"), tr("Value")});
    instanceParamTable_->horizontalHeader()->setStretchLastSection(true);
    instanceParamTable_->verticalHeader()->setVisible(false);
    instanceParamTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    instanceParamTable_->setMaximumHeight(120);
    instanceLayout->addWidget(instanceParamTable_);
    
    mainLayout->addWidget(instanceGroup);
    
    // Slice pattern section
    auto* sliceGroup = new QGroupBox(tr("Slice Patterns"), this);
    auto* sliceLayout = new QGridLayout(sliceGroup);
    
    sliceLayout->addWidget(new QLabel(tr("Begin Pattern:"), this), 0, 0);
    beginPatternEdit_ = new QLineEdit(this);
    beginPatternEdit_->setPlaceholderText(tr("Pattern for slice start"));
    sliceLayout->addWidget(beginPatternEdit_, 0, 1);
    
    sliceLayout->addWidget(new QLabel(tr("End Pattern:"), this), 1, 0);
    endPatternEdit_ = new QLineEdit(this);
    endPatternEdit_->setPlaceholderText(tr("Pattern for slice end"));
    sliceLayout->addWidget(endPatternEdit_, 1, 1);
    
    sliceLayout->addWidget(new QLabel(tr("Async ID Param:"), this), 2, 0);
    asyncIdParamEdit_ = new QLineEdit(this);
    asyncIdParamEdit_->setPlaceholderText(tr("e.g., request_id (optional)"));
    sliceLayout->addWidget(asyncIdParamEdit_, 2, 1);
    
    sliceParamTable_ = new QTableWidget(0, 2, this);
    sliceParamTable_->setHorizontalHeaderLabels({tr("Parameter"), tr("Value")});
    sliceParamTable_->horizontalHeader()->setStretchLastSection(true);
    sliceParamTable_->verticalHeader()->setVisible(false);
    sliceParamTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sliceParamTable_->setMaximumHeight(100);
    sliceLayout->addWidget(sliceParamTable_, 3, 0, 1, 2);
    
    mainLayout->addWidget(sliceGroup);
    
    // Set editability based on mode
    if (mode_ == EventDialogMode::SliceBegin) {
        endPatternEdit_->setReadOnly(true);
        endPatternEdit_->setPlaceholderText(tr("Will be defined later"));
    } else if (mode_ == EventDialogMode::SliceEnd) {
        beginPatternEdit_->setReadOnly(true);
        beginPatternEdit_->setPlaceholderText(tr("Already defined"));
    }
    
    // Status label
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    mainLayout->addWidget(statusLabel_);
    
    // Hint
    auto* hintLabel = new QLabel(
        tr("Hint: Use named capture groups like (?<name>pattern) to extract parameters."),
        this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet("QLabel { color: #888; }");
    mainLayout->addWidget(hintLabel);
    
    // Buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = buttonBox->button(QDialogButtonBox::Ok);
    mainLayout->addWidget(buttonBox);
    
    // Connect signals
    connect(instancePatternEdit_, &QLineEdit::textChanged, this, &EventConfigDialog::onPatternChanged);
    connect(beginPatternEdit_, &QLineEdit::textChanged, this, &EventConfigDialog::onPatternChanged);
    connect(endPatternEdit_, &QLineEdit::textChanged, this, &EventConfigDialog::onPatternChanged);
    connect(nameCombo_, &QComboBox::currentTextChanged, this, &EventConfigDialog::onNameChanged);
    connect(okButton_, &QPushButton::clicked, this, &EventConfigDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    // Initial visibility
    bool isInstance = (mode_ == EventDialogMode::Instance);
    instanceGroup->setVisible(isInstance);
    sliceGroup->setVisible(!isInstance);
}

void EventConfigDialog::populateExistingNames()
{
    const auto rules = klogg::ui::allEventRules();

    QSet<QString> addedNames;
    for (const auto& rule : rules) {
        const auto ruleName = QString::fromStdString( rule.name );
        // For SliceEnd mode, only show Slice events that have begin pattern
        if (mode_ == EventDialogMode::SliceEnd) {
            if (rule.eventType == EventType::Slice && !rule.beginPattern.empty()) {
                if (!addedNames.contains(ruleName)) {
                    addedNames.insert(ruleName);
                    nameCombo_->addItem(ruleName);
                }
            }
        } else {
            if (!addedNames.contains(ruleName)) {
                addedNames.insert(ruleName);
                nameCombo_->addItem(ruleName);
            }
        }
    }
    
    // For SliceEnd mode, auto-select first available
    if (mode_ == EventDialogMode::SliceEnd && nameCombo_->count() > 0) {
        nameCombo_->setCurrentIndex(0);
        loadExistingRule(nameCombo_->currentText());
    }
}

void EventConfigDialog::setEventRule(const EventRule& rule)
{
    currentRule_ = rule;
    
    nameCombo_->setCurrentText( QString::fromStdString( rule.name ) );
    descriptionEdit_->setPlainText( QString::fromStdString( rule.description ) );
    const auto category
        = rule.category.empty() ? QStringLiteral( "General" ) : QString::fromStdString( rule.category );
    categoryCombo_->setCurrentText( category );
    enabledCheck_->setChecked(rule.enabled);
    
    // Set event type
    for (int i = 0; i < eventTypeCombo_->count(); ++i) {
        if (eventTypeCombo_->itemData(i).toInt() == static_cast<int>(rule.eventType)) {
            eventTypeCombo_->setCurrentIndex(i);
            break;
        }
    }
    
    if (rule.eventType == EventType::Instance) {
        instancePatternEdit_->setText( QString::fromStdString( rule.pattern ) );
    } else {
        beginPatternEdit_->setText( QString::fromStdString( rule.beginPattern ) );
        endPatternEdit_->setText( QString::fromStdString( rule.endPattern ) );
        asyncIdParamEdit_->setText( QString::fromStdString( rule.asyncIdParam ) );
    }
    
    updateParameterTable();
    updateStatusLabel();
}

void EventConfigDialog::setOriginalText(const QString& text)
{
    originalText_ = text;
    updateParameterTable();
}

void EventConfigDialog::setInitialPattern(const QString& pattern)
{
    switch (mode_) {
        case EventDialogMode::Instance:
            instancePatternEdit_->setText(pattern);
            break;
        case EventDialogMode::SliceBegin:
        case EventDialogMode::EditSlice:
            beginPatternEdit_->setText(pattern);
            break;
        case EventDialogMode::SliceEnd:
            endPatternEdit_->setText(pattern);
            break;
    }
}

EventRule EventConfigDialog::getEventRule() const
{
    EventRule rule = currentRule_;
    rule.name = nameCombo_->currentText().trimmed().toStdString();
    rule.description = descriptionEdit_->toPlainText().toStdString();
    rule.category = categoryCombo_->currentText().toStdString();
    rule.eventType = static_cast<EventType>(eventTypeCombo_->currentData().toInt());
    
    if (rule.eventType == EventType::Instance) {
        rule.pattern = instancePatternEdit_->text().toStdString();
        rule.enabled = enabledCheck_->isChecked();
    } else {
        // Get patterns based on mode
        if (mode_ == EventDialogMode::SliceEnd) {
            // Preserve begin pattern, update end pattern
            rule.beginPattern = currentRule_.beginPattern;
            rule.endPattern = endPatternEdit_->text().toStdString();
        } else if (mode_ == EventDialogMode::SliceBegin) {
            // Update begin pattern, clear end pattern (will be added later)
            rule.beginPattern = beginPatternEdit_->text().toStdString();
            rule.endPattern.clear();
        } else {
            // EditSlice or Instance mode - get both patterns
            rule.beginPattern = beginPatternEdit_->text().toStdString();
            rule.endPattern = endPatternEdit_->text().toStdString();
        }
        rule.asyncIdParam = asyncIdParamEdit_->text().trimmed().toStdString();
        rule.enabled = !rule.beginPattern.empty() && !rule.endPattern.empty();
    }
    
    // Collect parameter names from table
    QTableWidget* table = (rule.eventType == EventType::Instance) ? instanceParamTable_ : sliceParamTable_;
    rule.parameterNames.clear();
    for (int i = 0; i < table->rowCount(); ++i) {
        QTableWidgetItem* item = table->item(i, 0);
        if ( item ) {
            const auto parameterName = item->text().toStdString();
            if ( std::find( rule.parameterNames.begin(), rule.parameterNames.end(), parameterName )
                 == rule.parameterNames.end() ) {
                rule.parameterNames.push_back( parameterName );
            }
        }
    }
    
    return rule;
}

void EventConfigDialog::onPatternChanged()
{
    updateParameterTable();
    updateStatusLabel();
}

void EventConfigDialog::onNameChanged(const QString& name)
{
    if (isExistingEventName(name)) {
        loadExistingRule(name);
    }
    updateStatusLabel();
}

void EventConfigDialog::onAccept()
{
    QString name = nameCombo_->currentText().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Validation Error"), tr("Event name cannot be empty."));
        return;
    }
    
    EventType eventType = static_cast<EventType>(eventTypeCombo_->currentData().toInt());
    auto validateRegex = [this]( const QString& pattern, const QString& label ) -> bool {
        QRegularExpression regex( pattern );
        if ( !regex.isValid() ) {
            QMessageBox::warning( this, tr( "Validation Error" ),
                                  tr( "Invalid %1 regular expression: %2" )
                                      .arg( label, regex.errorString() ) );
            return false;
        }
        return true;
    };

    if ( eventType == EventType::Instance ) {
        const QString pattern = instancePatternEdit_->text();
        if ( pattern.isEmpty() ) {
            QMessageBox::warning( this, tr( "Validation Error" ), tr( "Pattern cannot be empty." ) );
            return;
        }
        if ( !validateRegex( pattern, tr( "pattern" ) ) ) {
            return;
        }
    }
    else {
        QString beginPattern;
        QString endPattern;

        if ( mode_ == EventDialogMode::SliceEnd ) {
            beginPattern = QString::fromStdString( currentRule_.beginPattern );
            endPattern = endPatternEdit_->text();

            if ( beginPattern.isEmpty() ) {
                QMessageBox::warning( this, tr( "Validation Error" ),
                                      tr( "End pattern must be added to an existing event with begin pattern." ) );
                return;
            }
            if ( endPattern.isEmpty() ) {
                QMessageBox::warning( this, tr( "Validation Error" ), tr( "End pattern cannot be empty." ) );
                return;
            }
            if ( !validateRegex( endPattern, tr( "end pattern" ) ) ) {
                return;
            }
        }
        else if ( mode_ == EventDialogMode::EditSlice ) {
            beginPattern = beginPatternEdit_->text();
            endPattern = endPatternEdit_->text();

            if ( beginPattern.isEmpty() || endPattern.isEmpty() ) {
                QMessageBox::warning( this, tr( "Validation Error" ),
                                      tr( "Both begin pattern and end pattern are required for Slice events." ) );
                return;
            }
            if ( !validateRegex( beginPattern, tr( "begin pattern" ) )
                 || !validateRegex( endPattern, tr( "end pattern" ) ) ) {
                return;
            }
        }
        else {
            // SliceBegin: allow begin-only definition, end can be added later.
            beginPattern = beginPatternEdit_->text();
            endPattern = endPatternEdit_->text();

            if ( beginPattern.isEmpty() ) {
                QMessageBox::warning( this, tr( "Validation Error" ),
                                      tr( "Begin pattern cannot be empty." ) );
                return;
            }
            if ( !validateRegex( beginPattern, tr( "begin pattern" ) ) ) {
                return;
            }
            if ( !endPattern.isEmpty() && !validateRegex( endPattern, tr( "end pattern" ) ) ) {
                return;
            }
        }

        // Validate asyncIdParam for Slice events.
        const QString asyncIdParam = asyncIdParamEdit_->text().trimmed();
        if ( !asyncIdParam.isEmpty() ) {
            const QRegularExpression beginRegex( beginPattern );
            const QRegularExpression endRegex( endPattern );

            const bool inBegin = !beginPattern.isEmpty()
                                 && beginRegex.namedCaptureGroups().contains( asyncIdParam );
            const bool inEnd = endPattern.isEmpty()
                               || endRegex.namedCaptureGroups().contains( asyncIdParam );

            if ( !inBegin || !inEnd ) {
                QMessageBox::warning(
                    this, tr( "Validation Error" ),
                    tr( "asyncIdParam '%1' must exist in both beginPattern and endPattern.\n\n"
                        "Found in beginPattern: %2\n"
                        "Found in endPattern: %3" )
                        .arg( asyncIdParam )
                        .arg( inBegin ? tr( "Yes" ) : tr( "No" ) )
                        .arg( inEnd ? tr( "Yes" ) : tr( "No" ) ) );
                return;
            }
        }
    }
    
    // Check for duplicate name (only for new events)
    if ((mode_ == EventDialogMode::Instance || mode_ == EventDialogMode::SliceBegin) &&
        isExistingEventName(name)) {
        QMessageBox::warning(this, tr("Validation Error"), 
            tr("An event with name '%1' already exists.").arg(name));
        return;
    }
    
    accept();
}

void EventConfigDialog::updateParameterTable()
{
    QTableWidget* table = nullptr;
    QString pattern;
    
    EventType eventType = static_cast<EventType>(eventTypeCombo_->currentData().toInt());
    if (eventType == EventType::Instance) {
        table = instanceParamTable_;
        pattern = instancePatternEdit_->text();
    } else {
        table = sliceParamTable_;
        // Use currently focused pattern for parameter extraction
        if (!beginPatternEdit_->isReadOnly() && !beginPatternEdit_->text().isEmpty()) {
            pattern = beginPatternEdit_->text();
        } else if (!endPatternEdit_->isReadOnly()) {
            pattern = endPatternEdit_->text();
        }
    }
    
    table->setRowCount(0);
    if (pattern.isEmpty() || originalText_.isEmpty()) {
        return;
    }
    
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        return;
    }
    
    QRegularExpressionMatch match = regex.match(originalText_);
    if (!match.hasMatch()) {
        return;
    }
    
    for (const QString& paramName : regex.namedCaptureGroups()) {
        if (paramName.isEmpty()) {
            continue;
        }
        
        QString value = match.captured(paramName);
        if (value.isEmpty()) {
            continue;
        }
        
        int row = table->rowCount();
        table->insertRow(row);
        
        auto* nameItem = new QTableWidgetItem(paramName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, nameItem);
        
        auto* valueItem = new QTableWidgetItem(value);
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 1, valueItem);
    }
}

void EventConfigDialog::updateStatusLabel()
{
    if (eventTypeCombo_->currentData().toInt() != static_cast<int>(EventType::Slice)) {
        statusLabel_->clear();
        return;
    }
    
    QString begin = beginPatternEdit_->text();
    QString end = endPatternEdit_->text();
    
    bool hasBegin = !begin.isEmpty();
    bool hasEnd = !end.isEmpty();
    
    if (hasBegin && hasEnd) {
        statusLabel_->setText(tr("Complete - Event will be enabled"));
        statusLabel_->setStyleSheet("QLabel { color: green; }");
        enabledCheck_->setChecked(true);
    } else {
        QString missing = hasBegin ? tr("End pattern") : tr("Begin pattern");
        statusLabel_->setText(tr("Incomplete - %1 not defined").arg(missing));
        statusLabel_->setStyleSheet("QLabel { color: orange; }");
        enabledCheck_->setChecked(false);
    }
}

void EventConfigDialog::loadExistingRule(const QString& name)
{
    const auto rules = klogg::ui::allEventRules();

    for (const auto& rule : rules) {
        if ( QString::fromStdString( rule.name ) == name ) {
            currentRule_ = rule;
            
            descriptionEdit_->setPlainText( QString::fromStdString( rule.description ) );
            const auto category = rule.category.empty() ? QStringLiteral( "General" )
                                                        : QString::fromStdString( rule.category );
            categoryCombo_->setCurrentText( category );
            asyncIdParamEdit_->setText( QString::fromStdString( rule.asyncIdParam ) );
            
            // Set patterns based on mode
            if (mode_ == EventDialogMode::SliceEnd) {
                // Show existing begin pattern (read-only), clear end for user input
                beginPatternEdit_->setText( QString::fromStdString( rule.beginPattern ) );
                if (endPatternEdit_->text().isEmpty()) {
                    endPatternEdit_->setText( QString::fromStdString( rule.endPattern ) );
                }
            } else if (mode_ == EventDialogMode::SliceBegin) {
                beginPatternEdit_->setText( QString::fromStdString( rule.beginPattern ) );
                endPatternEdit_->setText( QString::fromStdString( rule.endPattern ) );
            } else {
                beginPatternEdit_->setText( QString::fromStdString( rule.beginPattern ) );
                endPatternEdit_->setText( QString::fromStdString( rule.endPattern ) );
            }
            
            updateStatusLabel();
            break;
        }
    }
}

bool EventConfigDialog::isExistingEventName(const QString& name) const
{
    if (name.isEmpty() || name == QString::fromStdString( currentRule_.name )) {
        return false;
    }

    const auto rules = klogg::ui::allEventRules();

    for (const auto& rule : rules) {
        if ( QString::fromStdString( rule.name ) == name ) {
            return true;
        }
    }
    return false;
}
