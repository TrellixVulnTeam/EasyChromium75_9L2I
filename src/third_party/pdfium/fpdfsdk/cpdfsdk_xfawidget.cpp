// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fpdfsdk/cpdfsdk_xfawidget.h"

#include "fpdfsdk/ipdfsdk_annothandler.h"
#include "xfa/fxfa/cxfa_ffwidget.h"

CPDFSDK_XFAWidget::CPDFSDK_XFAWidget(CXFA_FFWidget* pAnnot,
                                     CPDFSDK_PageView* pPageView,
                                     CPDFSDK_InteractiveForm* pInteractiveForm)
    : CPDFSDK_Annot(pPageView),
      m_pInteractiveForm(pInteractiveForm),
      m_hXFAWidget(pAnnot) {}

CPDFSDK_XFAWidget::~CPDFSDK_XFAWidget() = default;

bool CPDFSDK_XFAWidget::IsXFAField() const {
  return true;
}

CXFA_FFWidget* CPDFSDK_XFAWidget::GetXFAWidget() const {
  return m_hXFAWidget.Get();
}

CPDF_Annot::Subtype CPDFSDK_XFAWidget::GetAnnotSubtype() const {
  return CPDF_Annot::Subtype::XFAWIDGET;
}

CFX_FloatRect CPDFSDK_XFAWidget::GetRect() const {
  CFX_RectF rcBBox = GetXFAWidget()->GetLayoutItem()->GetRect(false);
  return CFX_FloatRect(rcBBox.left, rcBBox.top, rcBBox.left + rcBBox.width,
                       rcBBox.top + rcBBox.height);
}
