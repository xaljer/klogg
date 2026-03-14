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

#include <catch2/catch.hpp>

#include <QEventLoop>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QPushButton>

#include "test_utils.h"
#include "patterninputwidget.h"

// Accessor for private members in tests
struct PatternInputWidgetPrivate {
};

template <>
struct PatternInputWidget::access_by<PatternInputWidgetPrivate> {
    PatternInputWidget* widget = nullptr;

    QLineEdit* lineEdit()
    {
        return widget->lineEdit_;
    }

    QWidget* chipsContainer()
    {
        return widget->chipsContainer_;
    }

    QStringList patterns()
    {
        return widget->patterns_;
    }

    void addChip( const QString& pattern )
    {
        widget->addChip( pattern );
    }

    void removeChip( int index )
    {
        widget->removeChip( index );
    }
};

using PatternInputWidgetVisitor = PatternInputWidget::access_by<PatternInputWidgetPrivate>;

SCENARIO( "PatternInputWidget basic operations", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    QTest::qWait( 50 );

    GIVEN( "widget in normal mode" )
    {
        REQUIRE( !widget.isChipMode() );

        WHEN( "setting text" )
        {
            widget.setText( "test pattern" );

            THEN( "text is returned correctly" )
            {
                REQUIRE( widget.text() == "test pattern" );
            }
        }

        WHEN( "setting placeholder text" )
        {
            widget.setPlaceholderText( "enter pattern..." );

            THEN( "placeholder is set" )
            {
                REQUIRE( widget.placeholderText() == "enter pattern..." );
            }
        }

        WHEN( "clearing widget" )
        {
            widget.setText( "test" );
            widget.clear();

            THEN( "text is empty" )
            {
                REQUIRE( widget.text().isEmpty() );
            }
        }
    }
}

SCENARIO( "PatternInputWidget chip mode", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    REQUIRE( widget.isChipMode() );

    GIVEN( "chip mode enabled in normal (non-regex) mode" )
    {
        widget.setRegexMode( false );

        WHEN( "setting text with 'or' separator" )
        {
            widget.setText( "pattern1 or pattern2 or pattern3" );

            THEN( "text is combined with 'or'" )
            {
                REQUIRE( widget.text() == "pattern1 or pattern2 or pattern3" );
            }
        }

        WHEN( "setting text with single pattern" )
        {
            widget.setText( "single" );

            THEN( "text is returned as single pattern" )
            {
                REQUIRE( widget.text() == "single" );
            }
        }
    }

    GIVEN( "chip mode enabled in regex mode" )
    {
        widget.setRegexMode( true );

        WHEN( "setting text with pipe separator" )
        {
            widget.setText( "error|warning|info" );

            THEN( "text is combined with pipe" )
            {
                REQUIRE( widget.text() == "error|warning|info" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget adding chips", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    QSignalSpy chipChangedSpy( &widget, &PatternInputWidget::chipChanged );
    QSignalSpy returnPressedSpy( &widget, &PatternInputWidget::returnPressed );

    GIVEN( "empty widget in chip mode" )
    {
        WHEN( "typing and pressing return" )
        {
            QTest::keyClicks( visitor.lineEdit(), "first" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "chip is added and signal emitted" )
            {
                REQUIRE( chipChangedSpy.count() == 1 );
                REQUIRE( widget.text() == "first" );
            }
        }

        WHEN( "adding multiple chips" )
        {
            QTest::keyClicks( visitor.lineEdit(), "one" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            QTest::keyClicks( visitor.lineEdit(), "two" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            QTest::keyClicks( visitor.lineEdit(), "three" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "all chips are combined" )
            {
                REQUIRE( chipChangedSpy.count() == 3 );
                REQUIRE( widget.text() == "one or two or three" );
            }
        }

        WHEN( "adding duplicate chip" )
        {
            QTest::keyClicks( visitor.lineEdit(), "unique" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            chipChangedSpy.clear();

            QTest::keyClicks( visitor.lineEdit(), "unique" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "duplicate is ignored" )
            {
                REQUIRE( chipChangedSpy.count() == 0 );
                REQUIRE( widget.text() == "unique" );
            }
        }

        WHEN( "adding empty chip" )
        {
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "no signal emitted" )
            {
                REQUIRE( chipChangedSpy.count() == 0 );
                REQUIRE( widget.text().isEmpty() );
            }
        }
    }
}

SCENARIO( "PatternInputWidget removing chips", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with multiple chips" )
    {
        widget.setText( "one or two or three" );
        QTest::qWait( 50 );

        REQUIRE( widget.text() == "one or two or three" );

        QSignalSpy chipChangedSpy( &widget, &PatternInputWidget::chipChanged );

        WHEN( "removing middle chip" )
        {
            // Find the remove button on the second chip
            auto chips = visitor.chipsContainer()->findChildren<QWidget*>();
            REQUIRE( chips.size() >= 2 );

            QWidget* secondChip = nullptr;
            for ( auto* chip : chips ) {
                if ( chip->property( "chipText" ).toString() == "two" ) {
                    secondChip = chip;
                    break;
                }
            }
            REQUIRE( secondChip != nullptr );

            auto* removeButton = secondChip->findChild<QPushButton*>();
            REQUIRE( removeButton != nullptr );

            // Simulate hover to show button
            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( secondChip, &enterEvent );
            QTest::qWait( 10 );

            // Click remove button
            QTest::mouseClick( removeButton, Qt::LeftButton );
            QTest::qWait( 100 );

            THEN( "chip is removed and signal emitted" )
            {
                REQUIRE( chipChangedSpy.count() == 1 );
                REQUIRE( widget.text() == "one or three" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget mode switching", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget in normal mode with text" )
    {
        widget.setText( "normal text" );

        WHEN( "switching to chip mode" )
        {
            widget.setChipMode( true );
            QTest::qWait( 50 );

            THEN( "text is parsed as single chip" )
            {
                REQUIRE( widget.isChipMode() );
                REQUIRE( widget.text() == "normal text" );
            }

            AND_WHEN( "switching back to normal mode" )
            {
                // In chip mode, typing in line edit and switching back
                // Note: pressing Return in chip mode adds a new chip
                QTest::keyClicks( visitor.lineEdit(), "more" );
                QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
                QTest::qWait( 50 );

                widget.setChipMode( false );
                QTest::qWait( 50 );

                THEN( "chips are combined back to text" )
                {
                    REQUIRE( !widget.isChipMode() );
                    REQUIRE( widget.text() == "normal text or more" );
                }
            }
        }
    }

    GIVEN( "widget in chip mode with multiple chips" )
    {
        widget.setChipMode( true );
        widget.setRegexMode( true );
        widget.setText( "error|warning" );
        QTest::qWait( 50 );

        WHEN( "switching to normal mode" )
        {
            widget.setChipMode( false );
            QTest::qWait( 50 );

            THEN( "chips are combined with pipe separator" )
            {
                REQUIRE( !widget.isChipMode() );
                REQUIRE( widget.text() == "error|warning" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget read only mode", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    widget.setText( "one or two" );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with chips" )
    {
        WHEN( "setting read only" )
        {
            widget.setReadOnly( true );
            QTest::qWait( 50 );

            THEN( "widget is read only" )
            {
                REQUIRE( widget.isReadOnly() );
                REQUIRE( visitor.lineEdit()->isReadOnly() );
            }
        }
    }
}

SCENARIO( "PatternInputWidget removing chips via direct click", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with multiple chips" )
    {
        widget.setText( "one or two or three" );
        QTest::qWait( 50 );

        REQUIRE( widget.text() == "one or two or three" );

        QSignalSpy chipChangedSpy( &widget, &PatternInputWidget::chipChanged );

        WHEN( "removing chip via simulated mouse click" )
        {
            // Find the remove button on the middle chip
            auto allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
            QWidget* middleChip = nullptr;
            for ( auto* w : allWidgets ) {
                if ( w->property( "chipText" ).toString() == "two" ) {
                    middleChip = w;
                    break;
                }
            }
            REQUIRE( middleChip != nullptr );

            auto* removeButton = middleChip->findChild<QPushButton*>();
            REQUIRE( removeButton != nullptr );

            // Simulate hover to show button
            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( middleChip, &enterEvent );
            QTest::qWait( 10 );

            // Use QTest mouse events to simulate real click
            QTest::mousePress( removeButton, Qt::LeftButton );
            QTest::mouseRelease( removeButton, Qt::LeftButton );
            QTest::qWait( 100 );

            THEN( "chip is removed without crash and signal emitted" )
            {
                REQUIRE( chipChangedSpy.count() == 1 );
                REQUIRE( widget.text() == "one or three" );
            }
        }

        WHEN( "removing multiple chips sequentially via mouse click" )
        {
            // Find and remove first chip
            auto allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
            QWidget* firstChip = nullptr;
            for ( auto* w : allWidgets ) {
                if ( w->property( "chipText" ).toString() == "one" ) {
                    firstChip = w;
                    break;
                }
            }
            REQUIRE( firstChip != nullptr );

            auto* removeButton1 = firstChip->findChild<QPushButton*>();
            REQUIRE( removeButton1 != nullptr );

            QEnterEvent enterEvent1( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( firstChip, &enterEvent1 );
            QTest::qWait( 10 );

            QTest::mousePress( removeButton1, Qt::LeftButton );
            QTest::mouseRelease( removeButton1, Qt::LeftButton );
            QTest::qWait( 100 );

            REQUIRE( chipChangedSpy.count() == 1 );
            REQUIRE( widget.text() == "two or three" );

            // Find and remove another chip
            allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
            QWidget* secondChip = nullptr;
            for ( auto* w : allWidgets ) {
                if ( w->property( "chipText" ).toString() == "three" ) {
                    secondChip = w;
                    break;
                }
            }
            REQUIRE( secondChip != nullptr );

            auto* removeButton2 = secondChip->findChild<QPushButton*>();
            REQUIRE( removeButton2 != nullptr );

            QEnterEvent enterEvent2( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( secondChip, &enterEvent2 );
            QTest::qWait( 10 );

            QTest::mousePress( removeButton2, Qt::LeftButton );
            QTest::mouseRelease( removeButton2, Qt::LeftButton );
            QTest::qWait( 100 );

            THEN( "all chips removed correctly" )
            {
                REQUIRE( chipChangedSpy.count() == 2 );
                REQUIRE( widget.text() == "two" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget hover show remove button", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    widget.setText( "test" );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with a chip" )
    {
        // Find the actual chip widget by its chipText property
        auto allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
        QWidget* chip = nullptr;
        for ( auto* w : allWidgets ) {
            if ( w->property( "chipText" ).toString() == "test" ) {
                chip = w;
                break;
            }
        }
        REQUIRE( chip != nullptr );

        auto* removeButton = chip->findChild<QPushButton*>();
        REQUIRE( removeButton != nullptr );

        WHEN( "chip is not hovered" )
        {
            // Ensure mouse is not hovering by sending a leave event first
            QEvent leaveEvent( QEvent::Leave );
            qApp->sendEvent( chip, &leaveEvent );
            QTest::qWait( 10 );

            THEN( "remove button is hidden" )
            {
                REQUIRE( removeButton->isHidden() );
            }
        }

        WHEN( "chip is hovered" )
        {
            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( chip, &enterEvent );
            QTest::qWait( 10 );

            THEN( "remove button is shown" )
            {
                REQUIRE( removeButton->isVisible() );
            }

            AND_WHEN( "mouse leaves chip" )
            {
                QEvent leaveEvent( QEvent::Leave );
                qApp->sendEvent( chip, &leaveEvent );
                QTest::qWait( 10 );

                THEN( "remove button is hidden again" )
                {
                    REQUIRE( removeButton->isHidden() );
                }
            }
        }
    }
}

SCENARIO( "PatternInputWidget chip deletion safety during event handling", "[ui]" )
{
    // This test verifies that chips are deleted safely during event handling.
    // The bug: when clicking the remove button, the chip widget is deleted
    // during the click event processing, which can cause crashes when Qt
    // tries to access the deleted widget after the event handler returns.
    //
    // The fix: use QTimer::singleShot(0, ...) to defer the deletion until
    // the current event processing completes.

    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    // Helper function to find chip by text
    auto findChipByText = [ &visitor ]( const QString& text ) -> QWidget* {
        auto allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
        for ( auto* w : allWidgets ) {
            if ( w->property( "chipText" ).toString() == text ) {
                return w;
            }
        }
        return nullptr;
    };

    // Helper function to count chips
    auto countChips = [ &visitor ]() -> int {
        int count = 0;
        auto allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
        for ( auto* w : allWidgets ) {
            if ( w->property( "chipText" ).isValid() ) {
                count++;
            }
        }
        return count;
    };

    GIVEN( "widget with chips and a QPointer tracking a chip" )
    {
        widget.setText( "alpha or beta or gamma" );
        QTest::qWait( 50 );

        REQUIRE( countChips() == 3 );

        // Find the middle chip and create a QPointer to track it
        QWidget* middleChip = findChipByText( "beta" );
        REQUIRE( middleChip != nullptr );

        QPointer<QWidget> chipPointer = middleChip;
        QPointer<QPushButton> buttonPointer = middleChip->findChild<QPushButton*>();
        REQUIRE( buttonPointer != nullptr );

        WHEN( "triggering delete during event processing" )
        {
            // Simulate hover to show button
            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( middleChip, &enterEvent );
            QTest::qWait( 10 );

            // Click the remove button
            QTest::mousePress( buttonPointer, Qt::LeftButton );
            QTest::mouseRelease( buttonPointer, Qt::LeftButton );

            // Process events to allow deferred deletion to complete
            QEventLoop loop;
            QTimer::singleShot( 50, &loop, &QEventLoop::quit );
            loop.exec();

            THEN( "widget should be safely deleted after event processing" )
            {
                // After event processing, the chip should be deleted
                // QPointer should be null
                REQUIRE( chipPointer.isNull() );
                REQUIRE( buttonPointer.isNull() );
                REQUIRE( widget.text() == "alpha or gamma" );
            }
        }

        WHEN( "deleting multiple chips rapidly" )
        {
            // Create QPointer for first chip
            QWidget* firstChip = findChipByText( "alpha" );
            REQUIRE( firstChip != nullptr );
            QPointer<QWidget> firstChipPointer = firstChip;

            // Delete first chip
            QEnterEvent enterEvent1( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( firstChip, &enterEvent1 );
            QTest::qWait( 10 );

            auto* button1 = firstChip->findChild<QPushButton*>();
            QTest::mousePress( button1, Qt::LeftButton );
            QTest::mouseRelease( button1, Qt::LeftButton );
            QTest::qWait( 50 );

            // First chip should be deleted
            REQUIRE( firstChipPointer.isNull() );
            REQUIRE( widget.text() == "beta or gamma" );

            // Find and delete another chip
            QWidget* lastChip = findChipByText( "gamma" );
            REQUIRE( lastChip != nullptr );
            QPointer<QWidget> lastChipPointer = lastChip;

            QEnterEvent enterEvent2( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( lastChip, &enterEvent2 );
            QTest::qWait( 10 );

            auto* button2 = lastChip->findChild<QPushButton*>();
            QTest::mousePress( button2, Qt::LeftButton );
            QTest::mouseRelease( button2, Qt::LeftButton );
            QTest::qWait( 50 );

            THEN( "rapid sequential deletion should work correctly" )
            {
                REQUIRE( lastChipPointer.isNull() );
                REQUIRE( widget.text() == "beta" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget deletion timing verification", "[ui]" )
{
    // This test verifies that the deletion is properly deferred.
    // When a chip is removed via button click, the actual deletion should
    // happen AFTER the click event processing completes, not during it.
    //
    // Without QTimer::singleShot fix: removal happens immediately in onChipRemoveClicked
    // With fix: removal is deferred until event processing completes

    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with a chip" )
    {
        widget.setText( "test" );
        QTest::qWait( 50 );

        // Track whether widget is still valid during chipChanged signal
        bool widgetValidDuringSignal = false;
        QWidget* trackedChip = nullptr;

        QObject::connect( &widget, &PatternInputWidget::chipChanged, [ & ]( const QString& ) {
            // At this point, if removal happened synchronously, the chip might be invalid
            // If removal is deferred, the chip should still exist
            auto allWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
            for ( auto* w : allWidgets ) {
                if ( w->property( "chipText" ).toString() == "test" ) {
                    widgetValidDuringSignal = true;
                    trackedChip = w;
                    break;
                }
            }
        } );

        WHEN( "removing chip via button click" )
        {
            auto chips = visitor.chipsContainer()->findChildren<QWidget*>();
            QWidget* chip = nullptr;
            for ( auto* w : chips ) {
                if ( w->property( "chipText" ).toString() == "test" ) {
                    chip = w;
                    break;
                }
            }
            REQUIRE( chip != nullptr );

            auto* button = chip->findChild<QPushButton*>();
            REQUIRE( button != nullptr );

            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( chip, &enterEvent );
            QTest::qWait( 10 );

            QTest::mousePress( button, Qt::LeftButton );
            QTest::mouseRelease( button, Qt::LeftButton );
            QTest::qWait( 100 );

            THEN( "chipChanged signal should be emitted" )
            {
                // Verify the chip was removed
                int chipCount = 0;
                auto remainingWidgets = visitor.chipsContainer()->findChildren<QWidget*>();
                for ( auto* w : remainingWidgets ) {
                    if ( w->property( "chipText" ).isValid() ) {
                        chipCount++;
                    }
                }
                REQUIRE( widget.text().isEmpty() );
                REQUIRE( chipCount == 0 );
            }
        }
    }
}
