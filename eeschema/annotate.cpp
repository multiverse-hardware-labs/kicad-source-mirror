/**************************************/
/* annotate.cpp: component annotation */
/**************************************/

#include <algorithm> // to use sort vector
#include <vector>

#include "fctsys.h"
#include "common.h"
#include "class_drawpanel.h"
#include "confirm.h"
#include "wxstruct.h"
#include "class_sch_screen.h"
#include "wxEeschemaStruct.h"

#include "netlist.h"
#include "class_library.h"
#include "protos.h"
#include "sch_component.h"
#include "lib_pin.h"

//#define USE_OLD_ALGO

static void ComputeReferenceNumber( SCH_REFERENCE_LIST& aComponentsList, bool aUseSheetNum );
static int  ExistUnit( int aObjet, int aUnit, SCH_REFERENCE_LIST& aComponentList );

/**
 * Function DeleteAnnotation
 * Remove current component annotations
 * @param aCurrentSheetOnly : if false: remove all annotations, else remove
 *                             annotation relative to the current sheet only
 */
void SCH_EDIT_FRAME::DeleteAnnotation( bool aCurrentSheetOnly )
{
    if( aCurrentSheetOnly )
    {
        SCH_SCREEN* screen = GetScreen();
        wxCHECK_RET( screen != NULL, wxT( "Attempt to clear annotation of a NULL screen." ) );
        screen->ClearAnnotation( m_CurrentSheet );
    }
    else
    {
        SCH_SCREENS ScreenList;
        ScreenList.ClearAnnotation();
    }

    // Update the references for the sheet that is currently being displayed.
    m_CurrentSheet->UpdateAllScreenReferences();
}


/**
 * AnnotateComponents:
 *
 *  Compute the annotation of the components for the whole project, or the
 *  current sheet only.  All the components or the new ones only will be
 *  annotated.
 * @param parent = Schematic frame
 * @param annotateSchematic : true = entire schematic annotation,
 *                            false = current sheet only
 * @param sortOption : 0 = annotate by sorting X position,
 *                     1 = annotate by sorting Y position,
 *                     2 = annotate by sorting value
 * @param resetAnnotation : true = remove previous annotation
 *                          false = annotate new components only
 * @param repairsTimestamps : true = test for duplicate times stamps and
 *                                   replace duplicated
 *        Note: this option could change previous annotation, because time
 *              stamps are used to handle annotation mainly in complex
 *              hierarchies.
 */
void SCH_EDIT_FRAME::AnnotateComponents(
                         bool            annotateSchematic,
                         int             sortOption,
                         bool            resetAnnotation,
                         bool            repairsTimestamps )
{
    SCH_REFERENCE_LIST references;

    wxBusyCursor dummy;

    SCH_SCREENS screens;

    /* Build the sheet list */
    SCH_SHEET_LIST sheets;

    // Test for and replace duplicate time stamps in components and sheets.  Duplicate
    // time stamps can happen with old schematics, schematic conversions, or manual
    // editing of files.
    if( repairsTimestamps )
    {
        int count = screens.ReplaceDuplicateTimeStamps();

        if( count )
        {
            wxString msg;
            msg.Printf( _( "%d duplicate time stamps were found and replaced." ), count );
            DisplayInfoMessage( NULL, msg, 2 );
        }
    }

    // If it is an annotation for all the components, reset previous annotation.
    if( resetAnnotation )
        DeleteAnnotation( !annotateSchematic );

    // Update the screen date.
    screens.SetDate( GenDate() );

    // Set sheet number and number of sheets.
    SetSheetNumberAndCount();

    /* Build component list */
    if( annotateSchematic )
    {
        sheets.GetComponents( references );
    }
    else
    {
        GetSheet()->GetComponents( references );
    }

    /* Break full components reference in name (prefix) and number:
     * example: IC1 become IC, and 1 */
    references.SplitReferences( );

    bool useSheetNum = false;
    switch( sortOption )
    {
    case 0:
        references.SortCmpByXCoordinate();
        break;

    case 1:
        useSheetNum = true;
        references.SortCmpByXCoordinate();
        break;

    case 2:
        references.SortCmpByYCoordinate();
        break;

    case 3:
        useSheetNum = true;
        references.SortCmpByYCoordinate();
        break;

    case 4:
        references.SortComponentsByRefAndValue();
        break;
    }

    // Recalculate and update reference numbers in schematic
    ComputeReferenceNumber( references, useSheetNum );
    references.UpdateAnnotation();

    /* Final control (just in case ... )*/
    CheckAnnotate( NULL, !annotateSchematic );
    OnModify();

    // Update on screen refences, that can be modified by previous calculations:
    m_CurrentSheet->UpdateAllScreenReferences();
    SetSheetNumberAndCount();

    DrawPanel->Refresh( true );
}

#ifdef USE_OLD_ALGO
/** helper function
 * Search the last used (greatest) reference number in the component list
 * for the prefix reference given by Objet
 * The component list must be sorted.
 *
 * @param aObjet = reference item ( aComponentsList[aObjet].m_TextRef is
 *                 the search pattern)
 * @param aComponentsList = list of items
 * @param aMinValue = min value for the current search
 */
static int GetLastNumberInReference( int aObjet,SCH_REFERENCE_LIST& aComponentsList,
                              int aMinValue )
{
    int lastNumber = aMinValue;

    for( unsigned ii = 0; ii < aComponentsList.GetCount(); ii++ )
    {
        // search only for the current reference prefix:
        if( aComponentsList[aObjet].CompareRef( aComponentsList[ii] ) != 0 )
            continue;

        // update max value for the current reference prefix
        if( lastNumber < aComponentsList[ii].m_NumRef )
            lastNumber = aComponentsList[ii].m_NumRef;
    }

    return lastNumber;
}

#else
/**
 * helper function BuildRefIdInUseList
 * creates the list of reference numbers in use for a given reference prefix.
 * @param aObjet = the current component index to use for reference prefix filtering.
 * @param aComponentsList = the full list of components
 * @param aIdList = the buffer to fill
 * @param aMinRefId = the min id value to store. all values < aMinRefId are ignored
 */
static void  BuildRefIdInUseList( int aObjet,SCH_REFERENCE_LIST& aComponentsList,
                           std::vector<int>& aIdList, int aMinRefId )
{
    aIdList.clear();

    for( unsigned ii = 0; ii < aComponentsList.GetCount(); ii++ )
    {
        if(    ( aComponentsList[aObjet].CompareRef( aComponentsList[ii] ) == 0 )
            && ( aComponentsList[ii].m_NumRef >= aMinRefId ) )
            aIdList.push_back( aComponentsList[ii].m_NumRef );
    }
    sort( aIdList.begin(), aIdList.end() );

    // Ensure each reference Id appears only once
    // If there are multiple parts per package the same Id will be stored for each part.
    for( unsigned ii = 1; ii < aIdList.size(); ii++ )
    {
        if( aIdList[ii] != aIdList[ii-1] )
            continue;
        aIdList.erase(aIdList.begin() + ii );
        ii--;
    }
}

/**
 * helper function CreateFirstFreeRefId
 * Search for a free ref Id inside a list of reference numbers in use.
 * This list is expected sorted by increasing values, and each value stored only once
 * @see BuildRefIdInUseList to prepare this list
 * @param aIdList = the buffer that contains Ids in use
 * @param aFirstValue = the first expected free value
 * @return a free (not yet used) Id
 * and this new id is added in list
 */
static int CreateFirstFreeRefId(  std::vector<int>& aIdList, int aFirstValue )
{
    int expectedId = aFirstValue;

    // We search for expectedId a value >= aFirstValue.
    // Skip existing Id < aFirstValue
    unsigned ii = 0;
    for( ; ii < aIdList.size(); ii++ )
    {
        if( expectedId <= aIdList[ii]  )
            break;
    }

    // Ids are sorted by increasing value, from aFirstValue
    // So we search from aFirstValue the first not used value, i.e. the first hole in list.
     for(; ii < aIdList.size(); ii++ )
    {
        if( expectedId != aIdList[ii]  )    // This id is not yet used.
        {
            // Insert this free Id, in order to keep list sorted
            aIdList.insert(aIdList.begin() + ii, expectedId);
            return expectedId;
        }
        expectedId++;
    }

    // All existing Id are tested, and all values are found in use.
    // So Create a new one.
    aIdList.push_back( expectedId );
    return expectedId;
}
#endif

/*
 * Compute the reference number for components without reference number
 *  Compute .m_NumRef member
 */
static void ComputeReferenceNumber( SCH_REFERENCE_LIST& aComponentsList, bool aUseSheetNum  )
{
    if ( aComponentsList.GetCount() == 0 )
        return;

    int LastReferenceNumber = 0;
    int NumberOfUnits, Unit;

    /* Components with an invisible reference (power...) always are
     * re-annotated.  So set their .m_IsNew member to true
     */
    for( unsigned ii = 0; ii < aComponentsList.GetCount(); ii++ )
    {
        if( aComponentsList[ii].GetRefStr()[0] == '#' )
        {
            aComponentsList[ii].m_IsNew  = true;
            aComponentsList[ii].m_NumRef = 0;
        }
    }

    /* calculate index of the first component with the same reference prefix
     * than the current component.  All components having the same reference
     * prefix will receive a reference number with consecutive values:
     * IC .. will be set to IC4, IC4, IC5 ...
     */
    unsigned first = 0;

    /* calculate the last used number for this reference prefix: */
#ifdef USE_OLD_ALGO
    int minRefId = 0;
    // when using sheet number, ensure ref number >= sheet number* 100
    if( aUseSheetNum )
        minRefId = aComponentsList[first].m_SheetNum * 100;
    LastReferenceNumber = GetLastNumberInReference( first, aComponentsList, minRefId );
#else
    int minRefId = 1;
    // when using sheet number, ensure ref number >= sheet number* 100
    if( aUseSheetNum )
        minRefId = aComponentsList[first].m_SheetNum * 100 + 1;
    // This is the list of all Id already in use for a given reference prefix.
    // Will be refilled for each new reference prefix.
    std::vector<int>idList;
    BuildRefIdInUseList( first, aComponentsList, idList, minRefId );
#endif
    for( unsigned ii = 0; ii < aComponentsList.GetCount(); ii++ )
    {
        if( aComponentsList[ii].m_Flag )
            continue;
        if( ( aComponentsList[first].CompareRef( aComponentsList[ii] ) != 0 ) ||
            ( aUseSheetNum && ( aComponentsList[first].m_SheetNum != aComponentsList[ii].m_SheetNum ) )
            )
        {
            /* New reference found: we need a new ref number for this
             * reference */
            first = ii;
#ifdef USE_OLD_ALGO
            minRefId = 0;
            // when using sheet number, ensure ref number >= sheet number* 100
            if( aUseSheetNum )
                minRefId = aComponentsList[ii].m_SheetNum * 100;
            LastReferenceNumber = GetLastNumberInReference( ii, aComponentsList, minRefId);
#else
            minRefId = 1;
            // when using sheet number, ensure ref number >= sheet number* 100
            if( aUseSheetNum )
                minRefId = aComponentsList[ii].m_SheetNum * 100 + 1;
            BuildRefIdInUseList( first, aComponentsList, idList, minRefId );
#endif
        }
        /* Annotation of one part per package components (trivial case)*/
        if( aComponentsList[ii].m_Entry->GetPartCount() <= 1 )
        {
            if( aComponentsList[ii].m_IsNew )
            {
#ifdef USE_OLD_ALGO
                LastReferenceNumber++;
#else
                LastReferenceNumber = CreateFirstFreeRefId( idList, minRefId );
#endif
                aComponentsList[ii].m_NumRef = LastReferenceNumber;
            }

            aComponentsList[ii].m_Unit  = 1;
            aComponentsList[ii].m_Flag  = 1;
            aComponentsList[ii].m_IsNew = false;
            continue;
        }

        /* Annotation of multi-part components ( n parts per package )
         * (complex case) */
        NumberOfUnits = aComponentsList[ii].m_Entry->GetPartCount();

        if( aComponentsList[ii].m_IsNew )
        {
#ifdef USE_OLD_ALGO
            LastReferenceNumber++;
#else
            LastReferenceNumber = CreateFirstFreeRefId( idList, minRefId );
#endif
            aComponentsList[ii].m_NumRef = LastReferenceNumber;

            if( !aComponentsList[ii].IsPartsLocked() )
                aComponentsList[ii].m_Unit = 1;
            aComponentsList[ii].m_Flag = 1;
        }

        /* search for others units of this component.
         * we search for others parts that have the same value and the same
         * reference prefix (ref without ref number)
         */
        for( Unit = 1; Unit <= NumberOfUnits; Unit++ )
        {
            if( aComponentsList[ii].m_Unit == Unit )
                continue;

            int found = ExistUnit( ii, Unit, aComponentsList );

            if( found >= 0 )
                continue; /* this unit exists for this reference (unit
                           * already annotated) */

            /* Search a component to annotate ( same prefix, same value,
             * not annotated) */
            for( unsigned jj = ii + 1; jj < aComponentsList.GetCount(); jj++ )
            {
                if( aComponentsList[jj].m_Flag )    // already tested
                    continue;

                if( aComponentsList[ii].CompareRef( aComponentsList[jj] ) != 0 )
                    continue;

                if( aComponentsList[jj].CompareValue( aComponentsList[ii] ) != 0 )
                    continue;

                if( !aComponentsList[jj].m_IsNew )
                    continue;

                /* Component without reference number found, annotate it
                 * if possible */
                if( !aComponentsList[jj].IsPartsLocked()
                    || ( aComponentsList[jj].m_Unit == Unit ) )
                {
                    aComponentsList[jj].m_NumRef = aComponentsList[ii].m_NumRef;
                    aComponentsList[jj].m_Unit   = Unit;
                    aComponentsList[jj].m_Flag   = 1;
                    aComponentsList[jj].m_IsNew  = false;
                    break;
                }
            }
        }
    }
}


/**
 * Search in the sorted list of components, for a given component an other
 * component with the same reference and a given part unit.  Mainly used to
 * manage multiple parts per package components.
 * @param aObjet = index in aComponentsList for the given SCH_REFERENCE
 *                 item to test
 * @param Unit = the given unit number to search
 * @param aComponentsList = list of items to examine
 * @return index in aComponentsList if found or -1 if not found
 */
static int ExistUnit( int aObjet, int Unit, SCH_REFERENCE_LIST& aComponentsList )
{
    int NumRef;

    NumRef = aComponentsList[aObjet].m_NumRef;

    for( unsigned ii = 0; ii < aComponentsList.GetCount(); ii++ )
    {
        if( aObjet == (int) ii )
            // Do not compare with itself !
            continue;
        if( aComponentsList[ii].m_IsNew )
            // Not already with an updated reference
            continue;
        if( aComponentsList[ii].m_NumRef != NumRef )
            // Not the same reference number (like 35 in R35)
            continue;
        if( aComponentsList[aObjet].CompareRef( aComponentsList[ii] ) != 0 )
            // Not the same reference prefix
            continue;
        if( aComponentsList[ii].m_Unit == Unit )
        {
            // A part with the same reference and the given unit is found
            return ii;
        }
    }

    return -1;
}


/**
 * Function CheckAnnotate
 *  Check errors relatives to annotation:
 *      components not annotated
 *      components having the same reference (duplicates)
 *      for multiple parts per package components :
 *          part number > number of parts
 *          different values between parts
 * @param aMessageList = a wxArrayString to store messages. If NULL, they
 *                       are displayed in a wxMessageBox
 * @param aOneSheetOnly : true = search is made only in the current sheet
 *                        false = search in whole hierarchy (usual search).
 * @return errors count
 */
int SCH_EDIT_FRAME::CheckAnnotate( wxArrayString* aMessageList, bool aOneSheetOnly )
{
    int            error = 0;
    wxString       Buff;
    wxString       msg, cmpref;

    /* build the screen list */
    SCH_SHEET_LIST SheetList;

    SCH_REFERENCE_LIST ComponentsList;

    /* Build the list of components */
    if( !aOneSheetOnly )
        SheetList.GetComponents( ComponentsList );
    else
        GetSheet()->GetComponents( ComponentsList );

    ComponentsList.SortComponentsByRefAndValue();

    /* Break full components reference in name (prefix) and number: example:
     * IC1 become IC, and 1 */
    ComponentsList.SplitReferences();

    /* count not yet annotated items or annottaion error*/
    for( unsigned ii = 0; ii < ComponentsList.GetCount(); ii++ )
    {
        msg.Empty();
        Buff.Empty();

        if( ComponentsList[ii].m_IsNew )    // Not yet annotated
        {
            if( ComponentsList[ii].m_NumRef >= 0 )
                Buff << ComponentsList[ii].m_NumRef;
            else
                Buff = wxT( "?" );

            cmpref = ComponentsList[ii].GetRef();
            msg.Printf( _( "item not annotated: %s%s" ), GetChars( cmpref ), GetChars( Buff ) );

            if( ( ComponentsList[ii].m_Unit > 0 ) && ( ComponentsList[ii].m_Unit < 0x7FFFFFFF ) )
            {
                Buff.Printf( _( "( unit %d)" ), ComponentsList[ii].m_Unit );
                msg << Buff;
            }

            if( aMessageList )
                aMessageList->Add( msg + wxT( "\n" ) );
            else
                DisplayError( NULL, msg );

            error++;
            break;
        }

        // Annotate error if unit selected does not exist ( i.e. > number of parts )
        // Can happen if a component has changed in a lib, after a previous annotation
        if( MAX( ComponentsList[ii].m_Entry->GetPartCount(), 1 ) < ComponentsList[ii].m_Unit )
        {
            if( ComponentsList[ii].m_NumRef >= 0 )
                Buff << ComponentsList[ii].m_NumRef;
            else
                Buff = wxT( "?" );

            cmpref = ComponentsList[ii].GetRef();

            msg.Printf( _( "Error item %s%s" ), GetChars( cmpref ), GetChars( Buff ) );

            Buff.Printf( _( " unit %d and no more than %d parts" ),
                         ComponentsList[ii].m_Unit,
                         ComponentsList[ii].m_Entry->GetPartCount() );
            msg << Buff;

            if( aMessageList )
                aMessageList->Add( msg + wxT( "\n" ));
            else
                DisplayError( NULL, msg );

            error++;
            break;
        }
    }

    if( error )
        return error;

    // count the duplicated elements (if all are annotated)
    int imax = ComponentsList.GetCount() - 1;
    for( int ii = 0; (ii < imax) && (error < 4); ii++ )
    {
        msg.Empty();
        Buff.Empty();

        if( ( ComponentsList[ii].CompareRef( ComponentsList[ii + 1] ) != 0 )
           || ( ComponentsList[ii].m_NumRef != ComponentsList[ii + 1].m_NumRef ) )
            continue;

        /* Same reference found. If same unit, error !
         */
        if( ComponentsList[ii].m_Unit == ComponentsList[ii + 1].m_Unit )
        {
            if( ComponentsList[ii].m_NumRef >= 0 )
                Buff << ComponentsList[ii].m_NumRef;
            else
                Buff = wxT( "?" );

            cmpref = ComponentsList[ii].GetRef();

            msg.Printf( _( "Multiple item %s%s" ), GetChars( cmpref ), GetChars( Buff ) );

            if( ( ComponentsList[ii].m_Unit > 0 )&& ( ComponentsList[ii].m_Unit < 0x7FFFFFFF ) )
            {
                Buff.Printf( _( " (unit %d)" ), ComponentsList[ii].m_Unit );
                msg << Buff;
            }

            if( aMessageList )
                aMessageList->Add( msg + wxT( "\n" ));
            else
                DisplayError( NULL, msg );

            error++;
            continue;
        }

        /* Test error if units are different but number of parts per package
         * too high (ex U3 ( 1 part) and we find U3B this is an error) */
        if( ComponentsList[ii].m_Entry->GetPartCount()
            != ComponentsList[ii + 1].m_Entry->GetPartCount() )
        {
            if( ComponentsList[ii].m_NumRef >= 0 )
                Buff << ComponentsList[ii].m_NumRef;
            else
                Buff = wxT( "?" );

            cmpref = ComponentsList[ii].GetRef();
            msg.Printf( _( "Multiple item %s%s" ), GetChars( cmpref ), GetChars( Buff ) );

            if( ( ComponentsList[ii].m_Unit > 0 ) && ( ComponentsList[ii].m_Unit < 0x7FFFFFFF ) )
            {
                Buff.Printf( _( " (unit %d)" ), ComponentsList[ii].m_Unit );
                msg << Buff;
            }

            if( aMessageList )
                aMessageList->Add( msg + wxT( "\n" ));
            else
                DisplayError( NULL, msg );

            error++;
        }

        /* Error if values are different between units, for the same
         * reference */
        int next = ii + 1;

        if( ComponentsList[ii].CompareValue( ComponentsList[next] ) != 0 )
        {
            wxString nextcmpref = ComponentsList[next].GetRef();

            cmpref = ComponentsList[ii].GetRef();

#if defined(KICAD_GOST)
            msg.Printf( _( "Diff values for %s%d.%c (%s) and %s%d.%c (%s)" ),
                        cmpref.GetData(),
                        ComponentsList[ii].m_NumRef,
                        ComponentsList[ii].m_Unit + '1' - 1,
                        GetChars( *ComponentsList[ii].m_Value ),
                        GetChars( nextcmpref ),
                        ComponentsList[next].m_NumRef,
                        ComponentsList[next].m_Unit + '1' - 1,
                        ComponentsList[next].m_Value->GetData() );
#else
            msg.Printf( _( "Diff values for %s%d%c (%s) and %s%d%c (%s)" ),
                        cmpref.GetData(),
                        ComponentsList[ii].m_NumRef,
                        ComponentsList[ii].m_Unit + 'A' - 1,
                        GetChars( *ComponentsList[ii].m_Value ),
                        GetChars( nextcmpref ),
                        ComponentsList[next].m_NumRef,
                        ComponentsList[next].m_Unit + 'A' - 1,
                        GetChars( *ComponentsList[next].m_Value ) );
#endif

            if( aMessageList )
                aMessageList->Add( msg + wxT( "\n" ));
            else
                DisplayError( NULL, msg );

            error++;
        }
    }

    // count the duplicated time stamps
    ComponentsList.SortComponentsByTimeStamp();

    for( int ii = 0; ( ii < imax ) && ( error < 4 ); ii++ )
    {
        if( ( ComponentsList[ii].m_TimeStamp != ComponentsList[ii + 1].m_TimeStamp )
           || ( ComponentsList[ii].m_SheetPath != ComponentsList[ii + 1].m_SheetPath ) )
            continue;

        /* Same time stamp found.  */
        wxString nextcmpref;
        wxString full_path;

        full_path.Printf( wxT( "%s%8.8X" ),
                          GetChars( ComponentsList[ii].m_SheetPath.Path() ),
                          ComponentsList[ii].m_TimeStamp );

        cmpref     = ComponentsList[ii].GetRef();
        nextcmpref = ComponentsList[ii + 1].GetRef();

        msg.Printf( _( "duplicate time stamp (%s) for %s%d and %s%d" ),
                    GetChars( full_path ),
                    GetChars( cmpref ), ComponentsList[ii].m_NumRef,
                    GetChars( nextcmpref ), ComponentsList[ii + 1].m_NumRef );

        if( aMessageList )
            aMessageList->Add( msg + wxT( "\n" ));
        else
            DisplayError( NULL, msg );

        error++;
    }

    return error;
}
