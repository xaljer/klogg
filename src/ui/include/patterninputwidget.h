/*
 * Copyright (C) 2024 The klogg developers
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

#ifndef PATTERNINPUTWIDGET_H
#define PATTERNINPUTWIDGET_H

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

class QCompleter;

class PatternInputWidget : public QWidget {
    Q_OBJECT

  public:
    template <class T>
    struct access_by;

    explicit PatternInputWidget( QWidget* parent = nullptr );
    ~PatternInputWidget() = default;

    QString text() const;
    void setText( const QString& text );
    void setPatterns( const QStringList& patterns );
    QStringList patterns() const;

    void setPlaceholderText( const QString& placeholder );
    QString placeholderText() const;

    void focusInput( Qt::FocusReason reason = Qt::OtherFocusReason );

    void setRegexMode( bool isRegex );
    bool isRegexMode() const;

    void setReadOnly( bool readOnly );
    bool isReadOnly() const;

    void clear();

    void setChipMode( bool chipMode );
    bool isChipMode() const;

    void setSearchCompleter( QCompleter* completer );

  Q_SIGNALS:
    void textChanged( const QString& text );
    void chipChanged( const QString& text );
    void returnPressed();
    void contextMenuRequested( const QPoint& globalPos );

  public Q_SLOTS:
    void onChipRemoveClicked();

  private Q_SLOTS:
    void onLineEditReturnPressed();
    void onLineEditTextChanged( const QString& text );

  private:
    QStringList splitPattern( const QString& text ) const;
    void parsePatterns( const QString& text );
    QString combinePatterns() const;

    QWidget* createChipWidget( const QString& text, int index );
    void clearChips();
    void updateChips();
    void addChip( const QString& pattern );
    void removeChip( int index );
    void scrollToEnd();

    void startChipEdit( QLabel* label );
    void finishChipEdit( QLineEdit* edit, bool accept );
    void showHistoryMenu();

    bool eventFilter( QObject* obj, QEvent* event ) override;

    QHBoxLayout* mainLayout_ = nullptr;
    QScrollArea* chipsScrollArea_ = nullptr;
    QWidget* chipsContainer_ = nullptr;
    QHBoxLayout* chipsLayout_ = nullptr;
    QLineEdit* lineEdit_ = nullptr;
    QToolButton* historyButton_ = nullptr;
    QLineEdit* editingChipEdit_ = nullptr;

    QStringList patterns_;
    bool isRegexMode_ = false;
    bool isChipMode_ = false;
    bool isReadOnly_ = false;
};

#endif
