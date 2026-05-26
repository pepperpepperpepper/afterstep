#define LOCAL_DEBUG

#include "../../configure.h"

#include "asinternals.h"

#include "configure_internal.h"

void merge_feel (ASFeel * to, ASFeel * from)
{
	to->deprecated_flags = from->deprecated_flags;
	to->EdgeScrollX = from->EdgeScrollX;
	to->EdgeScrollY = from->EdgeScrollY;
	to->AutoRaiseDelay = from->AutoRaiseDelay;
	to->ClickTime = from->ClickTime;
	to->OpaqueMove = from->OpaqueMove;
	to->OpaqueResize = from->OpaqueResize;
	to->EdgeResistanceScroll = from->EdgeResistanceScroll;
	to->EdgeResistanceMove = from->EdgeResistanceMove;
	to->EdgeResistanceDragScroll = from->EdgeResistanceDragScroll;
	to->Xzap = from->Xzap;
	to->Yzap = from->Yzap;
	to->AutoReverse = from->AutoReverse;
	to->no_snaping_mod = from->no_snaping_mod;
	to->EdgeAttractionScreen = from->EdgeAttractionScreen;
	to->EdgeAttractionWindow = from->EdgeAttractionWindow;
	to->default_window_box_name = from->default_window_box_name;
	to->recent_submenu_items = from->recent_submenu_items;
	to->winlist_sort_order = from->winlist_sort_order;
	to->ShadeAnimationSteps = from->ShadeAnimationSteps;
	to->desk_cover_animation_steps = from->desk_cover_animation_steps;
	to->desk_cover_animation_type = from->desk_cover_animation_type;

}

void merge_look (MyLook * to, MyLook * from)
{
	int i;
	to->CursorFore = from->CursorFore;
	to->CursorBack = from->CursorBack;
	to->MenuArrow = from->MenuArrow;
	to->TitleTextAlign = from->TitleTextAlign;
	for (i = 0; i < 2; ++i) {
		to->TitleButtonSpacing[i] = from->TitleButtonSpacing[i];
		to->TitleButtonXOffset[i] = from->TitleButtonXOffset[i];
		to->TitleButtonYOffset[i] = from->TitleButtonYOffset[i];
	}
	to->TitleButtonStyle = from->TitleButtonStyle;
	to->resize_move_geometry = from->resize_move_geometry;
	to->StartMenuSortMode = from->StartMenuSortMode;
	to->DrawMenuBorders = from->DrawMenuBorders;
	to->ButtonWidth = (from->ButtonWidth == 0) ? 64 : from->ButtonWidth;
	to->ButtonHeight =
			(from->ButtonHeight == 0) ? to->ButtonWidth : from->ButtonHeight;
	to->ButtonIconSpacing = from->ButtonIconSpacing;
	to->ButtonAlign = from->ButtonAlign;
	to->ButtonBevel = from->ButtonBevel;

	to->minipixmap_width =
			(from->minipixmap_width == 0) ? 24 : from->minipixmap_width;
	to->minipixmap_height =
			(from->minipixmap_height ==
			 0) ? to->minipixmap_width : from->minipixmap_height;


	to->DefaultFrameName = from->DefaultFrameName;

	to->RubberBand = from->RubberBand;
	to->menu_icm = from->menu_icm;
	to->menu_hcm = from->menu_hcm;
	to->menu_scm = from->menu_scm;
	to->KillBackgroundThreshold = from->KillBackgroundThreshold;
	to->desktop_animation_tint = from->desktop_animation_tint;
	LOCAL_DEBUG_OUT ("desk_anime_tint = %lX", from->desktop_animation_tint);
}
