// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/accessibility/ax_view_obj_wrapper.h"

#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/platform/ax_unique_id.h"
#include "ui/views/accessibility/ax_aura_obj_cache.h"
#include "ui/views/accessibility/ax_virtual_view.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace views {

AXViewObjWrapper::AXViewObjWrapper(AXAuraObjCache* aura_obj_cache, View* view)
    : AXAuraObjWrapper(aura_obj_cache), view_(view) {
  if (view->GetWidget())
    aura_obj_cache_->GetOrCreate(view->GetWidget());
  view->AddObserver(this);
}

AXViewObjWrapper::~AXViewObjWrapper() {
  if (view_) {
    view_->RemoveObserver(this);
    view_ = nullptr;
  }
}

bool AXViewObjWrapper::IsIgnored() {
  return view_ ? view_->GetViewAccessibility().IsIgnored() : true;
}

AXAuraObjWrapper* AXViewObjWrapper::GetParent() {
  if (!view_)
    return nullptr;

  if (view_->parent())
    return aura_obj_cache_->GetOrCreate(view_->parent());

  if (view_->GetWidget())
    return aura_obj_cache_->GetOrCreate(view_->GetWidget());

  return nullptr;
}

void AXViewObjWrapper::GetChildren(
    std::vector<AXAuraObjWrapper*>* out_children) {
  if (!view_)
    return;

  const ViewAccessibility& view_accessibility = view_->GetViewAccessibility();
  if (view_accessibility.IsLeaf())
    return;

  // TODO(dtseng): Need to handle |Widget| child of |View|.
  for (View* child : view_->children()) {
    if (child->visible())
      out_children->push_back(aura_obj_cache_->GetOrCreate(child));
  }

  for (int i = 0; i < view_accessibility.virtual_child_count(); ++i) {
    AXVirtualView* child =
        const_cast<AXVirtualView*>(view_accessibility.virtual_child_at(i));
    out_children->push_back(child->GetOrCreateWrapper(aura_obj_cache_));
  }
}

void AXViewObjWrapper::Serialize(ui::AXNodeData* out_node_data) {
  if (!view_)
    return;

  ViewAccessibility& view_accessibility = view_->GetViewAccessibility();

  view_accessibility.GetAccessibleNodeData(out_node_data);
  out_node_data->id = GetUniqueId();

  if (view_accessibility.GetNextFocus()) {
    out_node_data->AddIntAttribute(
        ax::mojom::IntAttribute::kNextFocusId,
        aura_obj_cache_->GetID(view_accessibility.GetNextFocus()));
  }

  if (view_accessibility.GetPreviousFocus()) {
    out_node_data->AddIntAttribute(
        ax::mojom::IntAttribute::kPreviousFocusId,
        aura_obj_cache_->GetID(view_accessibility.GetPreviousFocus()));
  }
}

int32_t AXViewObjWrapper::GetUniqueId() const {
  return view_ ? view_->GetViewAccessibility().GetUniqueId() : -1;
}

bool AXViewObjWrapper::HandleAccessibleAction(const ui::AXActionData& action) {
  return view_ ? view_->HandleAccessibleAction(action) : false;
}

void AXViewObjWrapper::OnViewIsDeleting(View* observed_view) {
  view_ = nullptr;
}

}  // namespace views
