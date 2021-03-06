/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004-2020 KiCad Developers.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <common.h>
#include <class_board.h>
#include <class_drawsegment.h>

#include <geometry/polygon_test_point_inside.h>
#include <geometry/seg.h>
#include <geometry/shape_rect.h>
#include <geometry/shape_segment.h>

#include <drc/drc_engine.h>
#include <drc/drc_item.h>
#include <drc/drc_rule.h>
#include <drc/drc_test_provider_clearance_base.h>

#include <drc/drc_rtree.h>

/*
    Silk to silk clearance test. Check all silkscreen features against each other.
    Errors generated:
    - DRCE_SILK_CLEARANCE

*/

class DRC_TEST_PROVIDER_SILK_TO_SILK : public DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_SILK_TO_SILK ()
    {
    }

    virtual ~DRC_TEST_PROVIDER_SILK_TO_SILK() 
    {
    }

    virtual bool Run() override;

    virtual const wxString GetName() const override 
    {
        return "silk_to_silk"; 
    };

    virtual const wxString GetDescription() const override
    {
        return "Tests for overlapping silkscreen features.";
    }

    virtual int GetNumPhases() const override
    {
        return 1;
    }

    virtual std::set<DRC_CONSTRAINT_TYPE_T> GetConstraintTypes() const override;

private:

    BOARD* m_board;
    int m_largestClearance;
};


bool DRC_TEST_PROVIDER_SILK_TO_SILK::Run()
{
    m_board = m_drcEngine->GetBoard();

    DRC_CONSTRAINT worstClearanceConstraint;
    m_largestClearance = 0;

    if( m_drcEngine->QueryWorstConstraint( DRC_CONSTRAINT_TYPE_SILK_TO_SILK,
                                           worstClearanceConstraint, DRCCQ_LARGEST_MINIMUM ) )
    {
        m_largestClearance = worstClearanceConstraint.m_Value.Min();
    }

    reportAux( "Worst clearance : %d nm", m_largestClearance );
    reportPhase(( "Silkscreen clearances..." ));

    DRC_RTREE silkTree;

    auto addToTree =
            [&silkTree]( BOARD_ITEM *item ) -> bool
            {
                silkTree.insert( item );
                return true;
            };

    auto checkClearance =
            [&]( const DRC_RTREE::LAYER_PAIR& aLayers,
                 DRC_RTREE::ITEM_WITH_SHAPE* aRefItem,
                 DRC_RTREE::ITEM_WITH_SHAPE* aTestItem ) -> bool
            {
                if( m_drcEngine->IsErrorLimitExceeded( DRCE_SILK_CLEARANCE ) )
                    return false;

                auto constraint = m_drcEngine->EvalRulesForItems( DRC_CONSTRAINT_TYPE_SILK_TO_SILK,
                                                                  aRefItem->parent,
                                                                  aTestItem->parent );
                int      minClearance = constraint.GetValue().Min();
                int      actual;
                VECTOR2I pos;

                accountCheck( constraint );

                // only check for silkscreen collisions belonging to different modules or
                // overlapping texts

                KICAD_T typeRef = aRefItem->parent->Type();
                KICAD_T typeTest = aTestItem->parent->Type();

                MODULE *parentModRef = nullptr;
                MODULE *parentModTest = nullptr;

                if( typeRef == PCB_MODULE_EDGE_T || typeRef == PCB_MODULE_TEXT_T )
                {
                    parentModRef = static_cast<MODULE*> ( aRefItem->parent->GetParent() );
                }

                if( typeTest == PCB_MODULE_EDGE_T || typeTest == PCB_MODULE_TEXT_T )
                {
                    parentModTest = static_cast<MODULE*> ( aTestItem->parent->GetParent() );
                }

                // silkscreen drawings within the same module (or globally on the board)
                // don't report clearance errors. Everything else does.

                if( parentModRef && parentModRef == parentModTest )
                {
                    if( typeRef == PCB_MODULE_EDGE_T && typeTest == PCB_MODULE_EDGE_T )
                        return true;
                }

                if( !parentModRef && !parentModTest )
                {
                    if( typeRef == PCB_LINE_T && typeTest == PCB_LINE_T )
                        return true;
                }

                if( ! aRefItem->shape->Collide( aTestItem->shape, minClearance, &actual, &pos ) )
                    return true;

                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_SILK_CLEARANCE );
                wxString                  msg;

                msg.Printf( drcItem->GetErrorText() + _( " (%s clearance %s; actual %s)" ),
                            constraint.GetParentRule()->m_Name,
                            MessageTextFromValue( userUnits(), minClearance, true ),
                            MessageTextFromValue( userUnits(), actual, true ) );

                drcItem->SetErrorMessage( msg );
                drcItem->SetItems( aRefItem->parent, aTestItem->parent );
                drcItem->SetViolatingRule( constraint.GetParentRule() );

                reportViolation( drcItem, (wxPoint) pos );

                return true;
            };

    int numSilk = forEachGeometryItem( { PCB_LINE_T, PCB_MODULE_EDGE_T, PCB_TEXT_T, PCB_MODULE_TEXT_T },
                                       LSET( 2, F_SilkS, B_SilkS ), addToTree );

    reportAux( _("Testing %d silkscreen features."),  numSilk );

    const std::vector<DRC_RTREE::LAYER_PAIR> layerPairs =
    {
        DRC_RTREE::LAYER_PAIR( F_SilkS, F_SilkS ),
        DRC_RTREE::LAYER_PAIR( B_SilkS, B_SilkS )
    };

    silkTree.QueryCollidingPairs( &silkTree, layerPairs, checkClearance, m_largestClearance );

    reportRuleStatistics();

    return true;
}


std::set<DRC_CONSTRAINT_TYPE_T> DRC_TEST_PROVIDER_SILK_TO_SILK::GetConstraintTypes() const
{
    return { DRC_CONSTRAINT_TYPE_SILK_TO_SILK };
}


namespace detail
{
    static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_SILK_TO_SILK> dummy;
}