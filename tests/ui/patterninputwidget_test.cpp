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

    QVector<Chip> chips()
    {
        return widget->chips_;
    }

    void removeChip( int index )
    {
        widget->removeChip( index );
    }
};

using PatternInputWidgetVisitor = PatternInputWidget::access_by<PatternInputWidgetPrivate>;

// Helper: count chip widgets in the container
static int countChipWidgets( QWidget* container )
{
    int count = 0;
    auto widgets = container->findChildren<QWidget*>();
    for ( auto* w : widgets ) {
        if ( w->property( "chipText" ).isValid() ) {
            count++;
        }
    }
    return count;
}

// Helper: find a chip widget by chipText property
static QWidget* findChipWidget( QWidget* container, const QString& text )
{
    auto widgets = container->findChildren<QWidget*>();
    for ( auto* w : widgets ) {
        if ( w->property( "chipText" ).toString() == text ) {
            return w;
        }
    }
    return nullptr;
}

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

    GIVEN( "chip mode enabled in non-regex mode" )
    {
        widget.setRegexMode( false );

        WHEN( "setting text with boolean 'or' expression" )
        {
            widget.setText( "\"pattern1\" or \"pattern2\"" );

            THEN( "text generates correct boolean expression" )
            {
                REQUIRE( widget.text() == "(\"pattern1\" or \"pattern2\")" );
            }
        }

        WHEN( "setting text with single pattern" )
        {
            widget.setText( "single" );

            THEN( "text is returned as single pattern" )
            {
                REQUIRE( widget.text() == "\"single\"" );
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

SCENARIO( "PatternInputWidget adding chips via line edit", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    QSignalSpy chipChangedSpy( &widget, &PatternInputWidget::chipChanged );

    GIVEN( "empty widget in chip mode" )
    {
        WHEN( "typing and pressing return" )
        {
            QTest::keyClicks( visitor.lineEdit(), "error" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "chip is added and signal emitted" )
            {
                REQUIRE( chipChangedSpy.count() == 1 );
                REQUIRE( widget.text() == "\"error\"" );
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "error" } );
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

            THEN( "all chips are combined with OR" )
            {
                REQUIRE( chipChangedSpy.count() == 3 );
                REQUIRE( widget.chips().size() == 3 );
                REQUIRE( widget.text() == "(\"one\" or \"two\" or \"three\")" );
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
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.text() == "\"unique\"" );
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
                REQUIRE( widget.chips().isEmpty() );
            }
        }
    }
}

SCENARIO( "PatternInputWidget NOT button adds exclude chip", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    QSignalSpy chipChangedSpy( &widget, &PatternInputWidget::chipChanged );
    QSignalSpy notToggledSpy( &widget, &PatternInputWidget::notButtonToggled );

    GIVEN( "empty widget with NOT button visible" )
    {
        widget.setNotButtonVisible( true );

        WHEN( "enabling NOT button and typing chip" )
        {
            // The NOT button is checked programmatically to simulate user click
            // Then typing should create a Not chip
            REQUIRE( !widget.isNotButtonLit() );

            // We can't directly click the NOT button from the test,
            // but we can verify the Not chip creation path via setChips
            QVector<Chip> chips;
            chips.append( Chip{ ChipType::Not, { "timeout" } } );
            widget.setChips( chips );
            QTest::qWait( 50 );

            THEN( "exclude chip is displayed" )
            {
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Not );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "timeout" } );
                REQUIRE( widget.text() == "not(\"timeout\")" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget AND-group chip", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget in chip mode" )
    {
        WHEN( "adding AND-group via & syntax" )
        {
            QTest::keyClicks( visitor.lineEdit(), "error & panic" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "AND-group chip is created" )
            {
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::AndGroup );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "error", "panic" } );
                REQUIRE( widget.text() == "(\"error\" and \"panic\")" );
            }
        }

        WHEN( "adding NOT AND-group via & syntax with exclude" )
        {
            QVector<Chip> chips;
            chips.append( Chip{ ChipType::NotAndGroup, { "timeout", "debug" } } );
            widget.setChips( chips );
            QTest::qWait( 50 );

            THEN( "NOT AND-group chip is created" )
            {
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::NotAndGroup );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "timeout", "debug" } );
                REQUIRE( widget.text() == "not(\"timeout\" and \"debug\")" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget boolean expression to chip conversion", "[ui]" )
{
    GIVEN( "OR + NOT boolean expression" )
    {
        PatternInputWidget widget;
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing (\"error\" or \"warn\") and not(\"timeout\")" )
        {
            widget.setText( "(\"error\" or \"warn\") and not(\"timeout\")" );
            QTest::qWait( 50 );

            THEN( "chips are correct" )
            {
                REQUIRE( widget.chips().size() == 3 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "error" } );
                REQUIRE( widget.chips().at( 1 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "warn" } );
                REQUIRE( widget.chips().at( 2 ).type == ChipType::Not );
                REQUIRE( widget.chips().at( 2 ).terms == QStringList{ "timeout" } );
            }
        }
    }

    GIVEN( "AND-group + OR expression" )
    {
        PatternInputWidget widget;
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing (\"a\" and \"b\") or \"c\"" )
        {
            widget.setText( "(\"a\" and \"b\") or \"c\"" );
            QTest::qWait( 50 );

            THEN( "AND-group and OR chips are correct" )
            {
                REQUIRE( widget.chips().size() == 2 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::AndGroup );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "a", "b" } );
                REQUIRE( widget.chips().at( 1 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "c" } );
            }
        }
    }

    GIVEN( "NAND auto-conversion" )
    {
        PatternInputWidget widget;
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing \"a\" nand \"b\"" )
        {
            widget.setText( "\"a\" nand \"b\"" );
            QTest::qWait( 50 );

            THEN( "converted to NOT AND-group" )
            {
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::NotAndGroup );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "a", "b" } );
            }
        }
    }

    GIVEN( "NOR auto-conversion" )
    {
        PatternInputWidget widget;
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing \"a\" nor \"b\"" )
        {
            widget.setText( "\"a\" nor \"b\"" );
            QTest::qWait( 50 );

            THEN( "converted to two NOT chips" )
            {
                REQUIRE( widget.chips().size() == 2 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Not );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "a" } );
                REQUIRE( widget.chips().at( 1 ).type == ChipType::Not );
                REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "b" } );
            }
        }
    }
}

SCENARIO( "PatternInputWidget chip to boolean expression conversion", "[ui]" )
{
    GIVEN( "OR chips" )
    {
        PatternInputWidget widget;
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        widget.setChips( { Chip{ ChipType::Or, { "error" } },
                           Chip{ ChipType::Or, { "warn" } } } );
        QTest::qWait( 50 );

        THEN( "generates OR expression" )
        {
            REQUIRE( widget.text() == "(\"error\" or \"warn\")" );
        }
    }

    GIVEN( "OR + NOT chips" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        QTest::qWait( 50 );

        widget.setChips( { Chip{ ChipType::Or, { "error" } },
                           Chip{ ChipType::Or, { "warn" } },
                           Chip{ ChipType::Not, { "timeout" } } } );
        QTest::qWait( 50 );

        THEN( "generates OR + AND NOT expression" )
        {
            REQUIRE( widget.text() == "(\"error\" or \"warn\") and not(\"timeout\")" );
        }
    }

    GIVEN( "AND-group + OR chips" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        QTest::qWait( 50 );

        widget.setChips( { Chip{ ChipType::AndGroup, { "a", "b" } },
                           Chip{ ChipType::Or, { "c" } } } );
        QTest::qWait( 50 );

        THEN( "generates grouped AND + OR expression" )
        {
            REQUIRE( widget.text() == "((\"a\" and \"b\") or \"c\")" );
        }
    }
}

SCENARIO( "PatternInputWidget canParseToChips", "[ui]" )
{
    THEN( "simple OR is supported" )
    {
        REQUIRE( PatternInputWidget::canParseToChips( "\"a\" or \"b\"" ) );
    }

    THEN( "simple AND is supported" )
    {
        REQUIRE( PatternInputWidget::canParseToChips( "\"a\" and \"b\"" ) );
    }

    THEN( "OR + NOT is supported" )
    {
        REQUIRE( PatternInputWidget::canParseToChips(
            "(\"a\" or \"b\") and not(\"c\")" ) );
    }

    THEN( "XOR is NOT supported" )
    {
        REQUIRE( !PatternInputWidget::canParseToChips( "\"a\" xor \"b\"" ) );
    }

    THEN( "XNOR is NOT supported" )
    {
        REQUIRE( !PatternInputWidget::canParseToChips( "\"a\" xnor \"b\"" ) );
    }

    THEN( "mixed AND-OR at same level without parens is NOT supported" )
    {
        REQUIRE( !PatternInputWidget::canParseToChips( "\"a\" and \"b\" or \"c\"" ) );
    }

    THEN( "empty string is supported" )
    {
        REQUIRE( PatternInputWidget::canParseToChips( "" ) );
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
        widget.setChips( { Chip{ ChipType::Or, { "one" } },
                           Chip{ ChipType::Or, { "two" } },
                           Chip{ ChipType::Or, { "three" } } } );
        QTest::qWait( 50 );

        REQUIRE( widget.chips().size() == 3 );

        QSignalSpy chipChangedSpy( &widget, &PatternInputWidget::chipChanged );

        WHEN( "removing middle chip" )
        {
            QWidget* secondChip = findChipWidget( visitor.chipsContainer(), "two" );
            REQUIRE( secondChip != nullptr );

            auto* removeButton = secondChip->findChild<QPushButton*>();
            REQUIRE( removeButton != nullptr );

            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( secondChip, &enterEvent );
            QTest::qWait( 10 );

            QTest::mouseClick( removeButton, Qt::LeftButton );
            QTest::qWait( 100 );

            THEN( "chip is removed and signal emitted" )
            {
                REQUIRE( chipChangedSpy.count() == 1 );
                REQUIRE( widget.chips().size() == 2 );
                REQUIRE( widget.text() == "(\"one\" or \"three\")" );
            }
        }

        WHEN( "removing multiple chips sequentially" )
        {
            QWidget* firstChip = findChipWidget( visitor.chipsContainer(), "one" );
            REQUIRE( firstChip != nullptr );
            auto* btn1 = firstChip->findChild<QPushButton*>();
            QEnterEvent e1( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( firstChip, &e1 );
            QTest::qWait( 10 );
            QTest::mouseClick( btn1, Qt::LeftButton );
            QTest::qWait( 100 );

            REQUIRE( chipChangedSpy.count() == 1 );
            REQUIRE( widget.text() == "(\"two\" or \"three\")" );

            QWidget* thirdChip = findChipWidget( visitor.chipsContainer(), "three" );
            REQUIRE( thirdChip != nullptr );
            auto* btn2 = thirdChip->findChild<QPushButton*>();
            QEnterEvent e2( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( thirdChip, &e2 );
            QTest::qWait( 10 );
            QTest::mouseClick( btn2, Qt::LeftButton );
            QTest::qWait( 100 );

            THEN( "all chips removed correctly" )
            {
                REQUIRE( chipChangedSpy.count() == 2 );
                REQUIRE( widget.text() == "\"two\"" );
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
                REQUIRE( widget.text() == "\"normal text\"" );
                REQUIRE( widget.chips().size() == 1 );
            }
        }
    }

    GIVEN( "widget in chip mode with multiple chips" )
    {
        widget.setChipMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "error" } },
                           Chip{ ChipType::Or, { "warning" } } } );
        QTest::qWait( 50 );

        WHEN( "switching to normal mode" )
        {
            widget.setChipMode( false );
            QTest::qWait( 50 );

            THEN( "chips are combined back to text" )
            {
                REQUIRE( !widget.isChipMode() );
                REQUIRE( widget.text() == "(\"error\" or \"warning\")" );
            }
        }
    }

    GIVEN( "widget in chip mode with regex" )
    {
        widget.setChipMode( true );
        widget.setRegexMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "error" } },
                           Chip{ ChipType::Or, { "warning" } } } );
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

SCENARIO( "PatternInputWidget round-trip: chip mode off → on → off", "[ui]" )
{
    // This tests the exact flow: chips → turn off → turn on → chips preserved
    GIVEN( "chips are set, turned off, then turned on again" )
    {
        PatternInputWidget widget;
        widget.show();
        QTest::qWait( 50 );

        // Step 1: Set chips in chip mode
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "thread" } },
                           Chip{ ChipType::Or, { "xxx" } },
                           Chip{ ChipType::Or, { "processing" } },
                           Chip{ ChipType::Not, { "started" } } } );
        QTest::qWait( 50 );
        REQUIRE( widget.chips().size() == 4 );

        // Step 2: Turn chip mode off
        widget.setChipMode( false );
        QTest::qWait( 50 );
        REQUIRE( !widget.isChipMode() );
        REQUIRE( widget.text()
                 == "(\"thread\" or \"xxx\" or \"processing\") and not(\"started\")" );

        // Step 3: Turn chip mode back on
        widget.setChipMode( true );
        QTest::qWait( 50 );
        REQUIRE( widget.isChipMode() );

        THEN( "all chips are restored" )
        {
            REQUIRE( widget.chips().size() == 4 );
            REQUIRE( widget.chips().at( 0 ).type == ChipType::Or );
            REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "thread" } );
            REQUIRE( widget.chips().at( 1 ).type == ChipType::Or );
            REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "xxx" } );
            REQUIRE( widget.chips().at( 2 ).type == ChipType::Or );
            REQUIRE( widget.chips().at( 2 ).terms == QStringList{ "processing" } );
            REQUIRE( widget.chips().at( 3 ).type == ChipType::Not );
            REQUIRE( widget.chips().at( 3 ).terms == QStringList{ "started" } );
        }
    }
}

SCENARIO( "PatternInputWidget read only mode", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    widget.setChips( { Chip{ ChipType::Or, { "one" } }, Chip{ ChipType::Or, { "two" } } } );
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

SCENARIO( "PatternInputWidget hover show remove button", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    widget.setChips( { Chip{ ChipType::Or, { "test" } } } );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with a chip" )
    {
        QWidget* chip = findChipWidget( visitor.chipsContainer(), "test" );
        REQUIRE( chip != nullptr );

        auto* removeButton = chip->findChild<QPushButton*>();
        REQUIRE( removeButton != nullptr );

        WHEN( "chip is not hovered" )
        {
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
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "widget with chips and a QPointer tracking a chip" )
    {
        widget.setChips( { Chip{ ChipType::Or, { "alpha" } },
                           Chip{ ChipType::Or, { "beta" } },
                           Chip{ ChipType::Or, { "gamma" } } } );
        QTest::qWait( 50 );

        REQUIRE( countChipWidgets( visitor.chipsContainer() ) == 3 );

        QWidget* middleChip = findChipWidget( visitor.chipsContainer(), "beta" );
        REQUIRE( middleChip != nullptr );

        QPointer<QWidget> chipPointer = middleChip;
        QPointer<QPushButton> buttonPointer = middleChip->findChild<QPushButton*>();
        REQUIRE( buttonPointer != nullptr );

        WHEN( "triggering delete during event processing" )
        {
            QEnterEvent enterEvent( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( middleChip, &enterEvent );
            QTest::qWait( 10 );

            QTest::mousePress( buttonPointer, Qt::LeftButton );
            QTest::mouseRelease( buttonPointer, Qt::LeftButton );

            QEventLoop loop;
            QTimer::singleShot( 50, &loop, &QEventLoop::quit );
            loop.exec();

            THEN( "widget should be safely deleted after event processing" )
            {
                REQUIRE( chipPointer.isNull() );
                REQUIRE( buttonPointer.isNull() );
                REQUIRE( widget.chips().size() == 2 );
            }
        }

        WHEN( "deleting multiple chips rapidly" )
        {
            QWidget* firstChip = findChipWidget( visitor.chipsContainer(), "alpha" );
            REQUIRE( firstChip != nullptr );
            QPointer<QWidget> firstChipPointer = firstChip;

            QEnterEvent enterEvent1( QPointF( 0, 0 ), QPointF( 0, 0 ), QPointF( 0, 0 ) );
            qApp->sendEvent( firstChip, &enterEvent1 );
            QTest::qWait( 10 );

            auto* button1 = firstChip->findChild<QPushButton*>();
            QTest::mousePress( button1, Qt::LeftButton );
            QTest::mouseRelease( button1, Qt::LeftButton );
            QTest::qWait( 50 );

            REQUIRE( firstChipPointer.isNull() );
            REQUIRE( widget.chips().size() == 2 );

            QWidget* lastChip = findChipWidget( visitor.chipsContainer(), "gamma" );
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
                REQUIRE( widget.chips().size() == 1 );
                REQUIRE( widget.text() == "\"beta\"" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget regex pipe split in add chip", "[ui]" )
{
    PatternInputWidget widget;
    widget.show();
    widget.setChipMode( true );
    QTest::qWait( 50 );

    PatternInputWidgetVisitor visitor;
    visitor.widget = &widget;

    GIVEN( "chip mode with regex mode enabled" )
    {
        widget.setRegexMode( true );

        WHEN( "typing pipe-separated text and pressing return" )
        {
            QTest::keyClicks( visitor.lineEdit(), "error|warning" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "text is split into separate chips" )
            {
                REQUIRE( visitor.chips().size() == 2 );
                REQUIRE( visitor.chips().at( 0 ).type == ChipType::Or );
                REQUIRE( visitor.chips().at( 0 ).terms == QStringList{ "error" } );
                REQUIRE( visitor.chips().at( 1 ).type == ChipType::Or );
                REQUIRE( visitor.chips().at( 1 ).terms == QStringList{ "warning" } );
                REQUIRE( widget.text() == "error|warning" );
            }
        }

        WHEN( "typing three pipe-separated terms and pressing return" )
        {
            QTest::keyClicks( visitor.lineEdit(), "a|b|c" );
            QTest::keyClick( visitor.lineEdit(), Qt::Key_Return );
            QTest::qWait( 50 );

            THEN( "text is split into three chips" )
            {
                REQUIRE( visitor.chips().size() == 3 );
                REQUIRE( widget.text() == "a|b|c" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget simulate exclude-from-search flow", "[ui]" )
{
    // Reproduce the exact flow: user has OR chips, then triggers exclude
    // The generated expression must be a valid boolean expression with proper quoting
    GIVEN( "chips error+warn, exclude timeout" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "error" } },
                           Chip{ ChipType::Or, { "warn" } } } );
        QTest::qWait( 50 );

        REQUIRE( widget.chips().size() == 2 );

        // Simulate excludeFromSearch adding a NOT chip
        auto chips = widget.chips();
        chips.append( Chip{ ChipType::Not, { "timeout" } } );
        widget.setChips( chips );
        QTest::qWait( 50 );

        THEN( "generated expression has proper quoting for boolean engine" )
        {
            const auto expr = widget.text();
            REQUIRE( expr == "(\"error\" or \"warn\") and not(\"timeout\")" );
            REQUIRE( expr.contains( "\"error\"" ) );
            REQUIRE( expr.contains( "\"warn\"" ) );
            REQUIRE( expr.contains( "\"timeout\"" ) );
            // Verify no bare unquoted words between operators
            REQUIRE( !expr.contains( "error or" ) );
            REQUIRE( !expr.contains( "or warn" ) );
        }
    }

    GIVEN( "single OR chip, exclude with special characters" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "error" } } } );
        QTest::qWait( 50 );

        auto chips = widget.chips();
        chips.append( Chip{ ChipType::Not, { "connection timeout" } } );
        widget.setChips( chips );
        QTest::qWait( 50 );

        THEN( "multi-word term is properly quoted" )
        {
            REQUIRE( widget.text() == "\"error\" and not(\"connection timeout\")" );
        }
    }

    GIVEN( "only exclude chips" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        widget.setChips( { Chip{ ChipType::Not, { "debug" } },
                           Chip{ ChipType::Not, { "trace" } } } );
        QTest::qWait( 50 );

        THEN( "generates valid exclude-only expression" )
        {
            REQUIRE( widget.text() == "not(\"debug\") and not(\"trace\")" );
        }
    }

    GIVEN( "exclude with term containing quote characters" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "error" } } } );
        QTest::qWait( 50 );

        // Simulate user selecting text that contains actual quote chars
        // (e.g., log line has: Error "timeout" occurred)
        auto chips = widget.chips();
        chips.append( Chip{ ChipType::Not, { "\"timeout\"" } } );
        widget.setChips( chips );
        QTest::qWait( 50 );

        THEN( "inner quotes are escaped" )
        {
            const auto expr = widget.text();
            // The term "\"timeout\"" has actual quote characters.
            // They should be escaped as \" inside the not() quotes.
            REQUIRE( expr.contains( "not(" ) );
            REQUIRE( expr.contains( "\\\"timeout\\\"" ) );
        }
    }

    GIVEN( "regex mode with exclude chip" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        widget.setRegexMode( true );
        widget.setChips( { Chip{ ChipType::Or, { "error" } },
                           Chip{ ChipType::Not, { "timeout" } } } );
        QTest::qWait( 50 );

        THEN( "generates proper boolean expression even in regex mode" )
        {
            const auto expr = widget.text();
            // Must have quotes for boolean engine
            REQUIRE( expr.contains( "\"" ) );
            // NOT semantics preserved
            REQUIRE( expr.contains( "not(" ) );
        }
    }

    GIVEN( "three OR terms + one NOT — typical exclude-from-search result" )
    {
        PatternInputWidget widget;
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing (\"thread\" or \"xxx\" or \"processing\") and not(\"started\")" )
        {
            widget.setText(
                "(\"thread\" or \"xxx\" or \"processing\") and not(\"started\")" );
            QTest::qWait( 50 );

            THEN( "splits into 4 chips: 3 OR + 1 NOT" )
            {
                REQUIRE( widget.chips().size() == 4 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "thread" } );
                REQUIRE( widget.chips().at( 1 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "xxx" } );
                REQUIRE( widget.chips().at( 2 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 2 ).terms == QStringList{ "processing" } );
                REQUIRE( widget.chips().at( 3 ).type == ChipType::Not );
                REQUIRE( widget.chips().at( 3 ).terms == QStringList{ "started" } );
            }

            THEN( "round-trips to original expression" )
            {
                REQUIRE( widget.text()
                         == "(\"thread\" or \"xxx\" or \"processing\") and not(\"started\")" );
            }
        }
    }
}

SCENARIO( "PatternInputWidget regex + boolean mode with exclude", "[ui]" )
{
    // The exact bug: isRegexMode=true + isBooleanMode=true,
    // parsePatterns must use boolean expression path, not | split
    GIVEN( "regex on, boolean on, expression with NOT" )
    {
        PatternInputWidget widget;
        widget.setRegexMode( true );
        widget.setBooleanMode( true );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing \"ERROR\" and not(\"Database\")" )
        {
            widget.setText( "\"ERROR\" and not(\"Database\")" );
            QTest::qWait( 50 );

            THEN( "splits into 2 chips: OR + NOT" )
            {
                REQUIRE( widget.chips().size() == 2 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "ERROR" } );
                REQUIRE( widget.chips().at( 1 ).type == ChipType::Not );
                REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "Database" } );
            }

            THEN( "combinePatterns round-trips" )
            {
                REQUIRE( widget.text() == "\"ERROR\" and not(\"Database\")" );
            }
        }
    }

    GIVEN( "regex on, boolean off, pipe expression" )
    {
        PatternInputWidget widget;
        widget.setRegexMode( true );
        widget.setBooleanMode( false );
        widget.setChipMode( true );
        QTest::qWait( 50 );

        WHEN( "parsing error|warning" )
        {
            widget.setText( "error|warning" );
            QTest::qWait( 50 );

            THEN( "splits by |" )
            {
                REQUIRE( widget.chips().size() == 2 );
                REQUIRE( widget.chips().at( 0 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 0 ).terms == QStringList{ "error" } );
                REQUIRE( widget.chips().at( 1 ).type == ChipType::Or );
                REQUIRE( widget.chips().at( 1 ).terms == QStringList{ "warning" } );
                REQUIRE( widget.text() == "error|warning" );
            }
        }
    }
}
