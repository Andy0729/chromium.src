// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_MAC_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_MAC_H_

#import <Cocoa/Cocoa.h>
#include <IOSurface/IOSurfaceAPI.h>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/browser/compositor/browser_compositor_view_mac.h"
#include "content/browser/compositor/delegated_frame_host.h"
#include "content/browser/compositor/io_surface_layer_mac.h"
#include "content/browser/renderer_host/display_link_mac.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/renderer_host/software_frame_manager.h"
#include "content/common/content_export.h"
#include "content/common/cursors/webcursor.h"
#include "content/common/edit_command.h"
#import "content/public/browser/render_widget_host_view_mac_base.h"
#include "ipc/ipc_sender.h"
#include "third_party/WebKit/public/web/WebCompositionUnderline.h"
#include "ui/base/cocoa/base_view.h"
#include "ui/base/cocoa/remote_layer_api.h"
#include "ui/gfx/display_observer.h"

namespace content {
class BrowserCompositorviewMac;
class RenderWidgetHostViewMac;
class RenderWidgetHostViewMacEditCommandHelper;
class WebContents;
}

namespace ui {
class Compositor;
class Layer;
}

@class FullscreenWindowManager;
@protocol RenderWidgetHostViewMacDelegate;
@class ToolTip;

@protocol RenderWidgetHostViewMacOwner
- (content::RenderWidgetHostViewMac*)renderWidgetHostViewMac;
@end

// This is the view that lives in the Cocoa view hierarchy. In Windows-land,
// RenderWidgetHostViewWin is both the view and the delegate. We split the roles
// but that means that the view needs to own the delegate and will dispose of it
// when it's removed from the view system.
@interface RenderWidgetHostViewCocoa
    : BaseView <RenderWidgetHostViewMacBase,
                RenderWidgetHostViewMacOwner,
                NSTextInputClient> {
 @private
  scoped_ptr<content::RenderWidgetHostViewMac> renderWidgetHostView_;
  // This ivar is the cocoa delegate of the NSResponder.
  base::scoped_nsobject<NSObject<RenderWidgetHostViewMacDelegate>>
      responderDelegate_;
  BOOL canBeKeyView_;
  BOOL takesFocusOnlyOnMouseDown_;
  BOOL closeOnDeactivate_;
  scoped_ptr<content::RenderWidgetHostViewMacEditCommandHelper>
      editCommand_helper_;

  // These are part of the magic tooltip code from WebKit's WebHTMLView:
  id trackingRectOwner_;              // (not retained)
  void* trackingRectUserData_;
  NSTrackingRectTag lastToolTipTag_;
  base::scoped_nsobject<NSString> toolTip_;

  // Is YES if there was a mouse-down as yet unbalanced with a mouse-up.
  BOOL hasOpenMouseDown_;

  NSWindow* lastWindow_;  // weak

  // The cursor for the page. This is passed up from the renderer.
  base::scoped_nsobject<NSCursor> currentCursor_;

  // Variables used by our implementaion of the NSTextInput protocol.
  // An input method of Mac calls the methods of this protocol not only to
  // notify an application of its status, but also to retrieve the status of
  // the application. That is, an application cannot control an input method
  // directly.
  // This object keeps the status of a composition of the renderer and returns
  // it when an input method asks for it.
  // We need to implement Objective-C methods for the NSTextInput protocol. On
  // the other hand, we need to implement a C++ method for an IPC-message
  // handler which receives input-method events from the renderer.

  // Represents the input-method attributes supported by this object.
  base::scoped_nsobject<NSArray> validAttributesForMarkedText_;

  // Indicates if we are currently handling a key down event.
  BOOL handlingKeyDown_;

  // Indicates if there is any marked text.
  BOOL hasMarkedText_;

  // Indicates if unmarkText is called or not when handling a keyboard
  // event.
  BOOL unmarkTextCalled_;

  // The range of current marked text inside the whole content of the DOM node
  // being edited.
  // TODO(suzhe): This is currently a fake value, as we do not support accessing
  // the whole content yet.
  NSRange markedRange_;

  // The selected range, cached from a message sent by the renderer.
  NSRange selectedRange_;

  // Text to be inserted which was generated by handling a key down event.
  base::string16 textToBeInserted_;

  // Marked text which was generated by handling a key down event.
  base::string16 markedText_;

  // Underline information of the |markedText_|.
  std::vector<blink::WebCompositionUnderline> underlines_;

  // Indicates if doCommandBySelector method receives any edit command when
  // handling a key down event.
  BOOL hasEditCommands_;

  // Contains edit commands received by the -doCommandBySelector: method when
  // handling a key down event, not including inserting commands, eg. insertTab,
  // etc.
  content::EditCommands editCommands_;

  // The plugin that currently has focus (-1 if no plugin has focus).
  int focusedPluginIdentifier_;

  // Whether or not plugin IME is currently enabled active.
  BOOL pluginImeActive_;

  // Whether the previous mouse event was ignored due to hitTest check.
  BOOL mouseEventWasIgnored_;

  // Event monitor for scroll wheel end event.
  id endWheelMonitor_;

  // If true then escape key down events are suppressed until the first escape
  // key up event. (The up event is suppressed as well). This is used by the
  // flash fullscreen code to avoid sending a key up event without a matching
  // key down event.
  BOOL suppressNextEscapeKeyUp_;
}

@property(nonatomic, readonly) NSRange selectedRange;
@property(nonatomic, readonly) BOOL suppressNextEscapeKeyUp;

- (void)setCanBeKeyView:(BOOL)can;
- (void)setTakesFocusOnlyOnMouseDown:(BOOL)b;
- (void)setCloseOnDeactivate:(BOOL)b;
- (void)setToolTipAtMousePoint:(NSString *)string;
// True for always-on-top special windows (e.g. Balloons and Panels).
- (BOOL)acceptsMouseEventsWhenInactive;
// Cancel ongoing composition (abandon the marked text).
- (void)cancelComposition;
// Confirm ongoing composition.
- (void)confirmComposition;
// Enables or disables plugin IME.
- (void)setPluginImeActive:(BOOL)active;
// Updates the current plugin focus state.
- (void)pluginFocusChanged:(BOOL)focused forPlugin:(int)pluginId;
// Evaluates the event in the context of plugin IME, if plugin IME is enabled.
// Returns YES if the event was handled.
- (BOOL)postProcessEventForPluginIme:(NSEvent*)event;
- (void)updateCursor:(NSCursor*)cursor;
- (NSRect)firstViewRectForCharacterRange:(NSRange)theRange
                             actualRange:(NSRangePointer)actualRange;
@end

namespace content {
class RenderWidgetHostImpl;

///////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewMac
//
//  An object representing the "View" of a rendered web page. This object is
//  responsible for displaying the content of the web page, and integrating with
//  the Cocoa view system. It is the implementation of the RenderWidgetHostView
//  that the cross-platform RenderWidgetHost object uses
//  to display the data.
//
//  Comment excerpted from render_widget_host.h:
//
//    "The lifetime of the RenderWidgetHost* is tied to the render process.
//     If the render process dies, the RenderWidgetHost* goes away and all
//     references to it must become NULL."
//
// RenderWidgetHostView class hierarchy described in render_widget_host_view.h.
class CONTENT_EXPORT RenderWidgetHostViewMac
    : public RenderWidgetHostViewBase,
      public DelegatedFrameHostClient,
      public BrowserCompositorViewMacClient,
      public IPC::Sender,
      public gfx::DisplayObserver {
 public:
  // The view will associate itself with the given widget. The native view must
  // be hooked up immediately to the view hierarchy, or else when it is
  // deleted it will delete this out from under the caller.
  //
  // When |is_guest_view_hack| is true, this view isn't really the view for
  // the |widget|, a RenderWidgetHostViewGuest is.
  // TODO(lazyboy): Remove |is_guest_view_hack| once BrowserPlugin has migrated
  // to use RWHVChildFrame (http://crbug.com/330264).
  RenderWidgetHostViewMac(RenderWidgetHost* widget, bool is_guest_view_hack);
  virtual ~RenderWidgetHostViewMac();

  RenderWidgetHostViewCocoa* cocoa_view() const { return cocoa_view_; }

  // |delegate| is used to separate out the logic from the NSResponder delegate.
  // |delegate| is retained by this class.
  // |delegate| should be set at most once.
  CONTENT_EXPORT void SetDelegate(
    NSObject<RenderWidgetHostViewMacDelegate>* delegate);
  void SetAllowPauseForResizeOrRepaint(bool allow);

  // RenderWidgetHostView implementation.
  virtual bool OnMessageReceived(const IPC::Message& msg) override;
  virtual void InitAsChild(gfx::NativeView parent_view) override;
  virtual RenderWidgetHost* GetRenderWidgetHost() const override;
  virtual void SetSize(const gfx::Size& size) override;
  virtual void SetBounds(const gfx::Rect& rect) override;
  virtual gfx::Vector2dF GetLastScrollOffset() const override;
  virtual gfx::NativeView GetNativeView() const override;
  virtual gfx::NativeViewId GetNativeViewId() const override;
  virtual gfx::NativeViewAccessible GetNativeViewAccessible() override;
  virtual bool HasFocus() const override;
  virtual bool IsSurfaceAvailableForCopy() const override;
  virtual void Show() override;
  virtual void Hide() override;
  virtual bool IsShowing() override;
  virtual gfx::Rect GetViewBounds() const override;
  virtual void SetShowingContextMenu(bool showing) override;
  virtual void SetActive(bool active) override;
  virtual void SetTakesFocusOnlyOnMouseDown(bool flag) override;
  virtual void SetWindowVisibility(bool visible) override;
  virtual void WindowFrameChanged() override;
  virtual void ShowDefinitionForSelection() override;
  virtual bool SupportsSpeech() const override;
  virtual void SpeakSelection() override;
  virtual bool IsSpeaking() const override;
  virtual void StopSpeaking() override;
  virtual void SetBackgroundOpaque(bool opaque) override;

  // Implementation of RenderWidgetHostViewBase.
  virtual void InitAsPopup(RenderWidgetHostView* parent_host_view,
                           const gfx::Rect& pos) override;
  virtual void InitAsFullscreen(
      RenderWidgetHostView* reference_host_view) override;
  virtual void WasShown() override;
  virtual void WasHidden() override;
  virtual void MovePluginWindows(
      const std::vector<WebPluginGeometry>& moves) override;
  virtual void Focus() override;
  virtual void Blur() override;
  virtual void UpdateCursor(const WebCursor& cursor) override;
  virtual void SetIsLoading(bool is_loading) override;
  virtual void TextInputTypeChanged(ui::TextInputType type,
                                    ui::TextInputMode input_mode,
                                    bool can_compose_inline) override;
  virtual void ImeCancelComposition() override;
  virtual void ImeCompositionRangeChanged(
      const gfx::Range& range,
      const std::vector<gfx::Rect>& character_bounds) override;
  virtual void RenderProcessGone(base::TerminationStatus status,
                                 int error_code) override;
  virtual void RenderWidgetHostGone() override;
  virtual void Destroy() override;
  virtual void SetTooltipText(const base::string16& tooltip_text) override;
  virtual void SelectionChanged(const base::string16& text,
                                size_t offset,
                                const gfx::Range& range) override;
  virtual void SelectionBoundsChanged(
      const ViewHostMsg_SelectionBounds_Params& params) override;
  virtual void CopyFromCompositingSurface(
      const gfx::Rect& src_subrect,
      const gfx::Size& dst_size,
      const base::Callback<void(bool, const SkBitmap&)>& callback,
      SkColorType color_type) override;
  virtual void CopyFromCompositingSurfaceToVideoFrame(
      const gfx::Rect& src_subrect,
      const scoped_refptr<media::VideoFrame>& target,
      const base::Callback<void(bool)>& callback) override;
  virtual bool CanCopyToVideoFrame() const override;
  virtual bool CanSubscribeFrame() const override;
  virtual void BeginFrameSubscription(
      scoped_ptr<RenderWidgetHostViewFrameSubscriber> subscriber) override;
  virtual void EndFrameSubscription() override;
  virtual void OnSwapCompositorFrame(
      uint32 output_surface_id, scoped_ptr<cc::CompositorFrame> frame) override;
  virtual BrowserAccessibilityManager* CreateBrowserAccessibilityManager(
      BrowserAccessibilityDelegate* delegate) override;
  virtual gfx::Point AccessibilityOriginInScreen(const gfx::Rect& bounds)
      override;
  virtual void AccessibilityShowMenu(const gfx::Point& point) override;
  virtual bool PostProcessEventForPluginIme(
      const NativeWebKeyboardEvent& event) override;

  virtual bool HasAcceleratedSurface(const gfx::Size& desired_size) override;
  virtual void GetScreenInfo(blink::WebScreenInfo* results) override;
  virtual gfx::Rect GetBoundsInRootWindow() override;
  virtual gfx::GLSurfaceHandle GetCompositingSurface() override;

  virtual bool LockMouse() override;
  virtual void UnlockMouse() override;
  virtual void WheelEventAck(const blink::WebMouseWheelEvent& event,
                             InputEventAckState ack_result) override;

  // IPC::Sender implementation.
  virtual bool Send(IPC::Message* message) override;

  virtual SkColorType PreferredReadbackFormat() override;

  // gfx::DisplayObserver implementation.
  virtual void OnDisplayAdded(const gfx::Display& new_display) override;
  virtual void OnDisplayRemoved(const gfx::Display& old_display) override;
  virtual void OnDisplayMetricsChanged(const gfx::Display& display,
                                       uint32_t metrics) override;

  // Forwards the mouse event to the renderer.
  void ForwardMouseEvent(const blink::WebMouseEvent& event);

  void KillSelf();

  void SetTextInputActive(bool active);

  // Sends completed plugin IME notification and text back to the renderer.
  void PluginImeCompositionCompleted(const base::string16& text, int plugin_id);

  const std::string& selected_text() const { return selected_text_; }

  // Returns true and stores first rectangle for character range if the
  // requested |range| is already cached, otherwise returns false.
  // Exposed for testing.
  CONTENT_EXPORT bool GetCachedFirstRectForCharacterRange(
      NSRange range, NSRect* rect, NSRange* actual_range);

  // Returns true if there is line break in |range| and stores line breaking
  // point to |line_breaking_point|. The |line_break_point| is valid only if
  // this function returns true.
  bool GetLineBreakIndex(const std::vector<gfx::Rect>& bounds,
                         const gfx::Range& range,
                         size_t* line_break_point);

  // Returns composition character boundary rectangle. The |range| is
  // composition based range. Also stores |actual_range| which is corresponding
  // to actually used range for returned rectangle.
  gfx::Rect GetFirstRectForCompositionRange(const gfx::Range& range,
                                            gfx::Range* actual_range);

  // Converts from given whole character range to composition oriented range. If
  // the conversion failed, return gfx::Range::InvalidRange.
  gfx::Range ConvertCharacterRangeToCompositionRange(
      const gfx::Range& request_range);

  WebContents* GetWebContents();

  // These member variables should be private, but the associated ObjC class
  // needs access to them and can't be made a friend.

  // The associated Model.  Can be NULL if Destroy() is called when
  // someone (other than superview) has retained |cocoa_view_|.
  RenderWidgetHostImpl* render_widget_host_;

  // Current text input type.
  ui::TextInputType text_input_type_;
  bool can_compose_inline_;

  // The background CoreAnimation layer which is hosted by |cocoa_view_|.
  base::scoped_nsobject<CALayer> background_layer_;

  // Delegated frame management and compositior.
  scoped_ptr<DelegatedFrameHost> delegated_frame_host_;
  scoped_ptr<ui::Layer> root_layer_;

  // Container for the NSView drawn by the browser compositor.
  scoped_ptr<BrowserCompositorViewMac> browser_compositor_view_;

  // Placeholder that is allocated while browser_compositor_view_ is NULL,
  // indicating that a BrowserCompositorViewMac may be allocated. This is to
  // help in recycling the internals of BrowserCompositorViewMac.
  scoped_ptr<BrowserCompositorViewPlaceholderMac>
      browser_compositor_view_placeholder_;

  NSWindow* pepper_fullscreen_window() const {
    return pepper_fullscreen_window_;
  }

  CONTENT_EXPORT void release_pepper_fullscreen_window_for_testing();

  RenderWidgetHostViewMac* fullscreen_parent_host_view() const {
    return fullscreen_parent_host_view_;
  }

  int window_number() const;

  // The scale factor for the screen that the view is currently on.
  float ViewScaleFactor() const;

  // Update the scale factor for the backing store and for any CALayers.
  void UpdateBackingStoreScaleFactor();

  // Ensure that the display link is associated with the correct display.
  void UpdateDisplayLink();

  void PauseForPendingResizeOrRepaintsAndDraw();

  // DelegatedFrameHostClient implementation.
  virtual ui::Compositor* GetCompositor() const override;
  virtual ui::Layer* GetLayer() override;
  virtual RenderWidgetHostImpl* GetHost() override;
  virtual bool IsVisible() override;
  virtual scoped_ptr<ResizeLock> CreateResizeLock(
      bool defer_compositor_lock) override;
  virtual gfx::Size DesiredFrameSize() override;
  virtual float CurrentDeviceScaleFactor() override;
  virtual gfx::Size ConvertViewSizeToPixel(const gfx::Size& size) override;
  virtual DelegatedFrameHost* GetDelegatedFrameHost() const override;

  // BrowserCompositorViewMacClient implementation.
  virtual bool BrowserCompositorViewShouldAckImmediately() const override;
  virtual void BrowserCompositorViewFrameSwapped(
      const std::vector<ui::LatencyInfo>& latency_info) override;

 private:
  friend class RenderWidgetHostViewMacTest;

  // Returns whether this render view is a popup (autocomplete window).
  bool IsPopup() const;

  // Shuts down the render_widget_host_.  This is a separate function so we can
  // invoke it from the message loop.
  void ShutdownHost();

  // Tear down all components of the browser compositor in an order that will
  // ensure no dangling references.
  void ShutdownBrowserCompositor();

  void EnsureBrowserCompositorView();
  void DestroyBrowserCompositorView();

  // IPC message handlers.
  void OnPluginFocusChanged(bool focused, int plugin_id);
  void OnStartPluginIme();
  void OnGetRenderedTextCompleted(const std::string& text);

  // Send updated vsync parameters to the renderer.
  void SendVSyncParametersToRenderer();

  // Dispatches a TTS session.
  void SpeakText(const std::string& text);

  // The associated view. This is weak and is inserted into the view hierarchy
  // to own this RenderWidgetHostViewMac object. Set to nil at the start of the
  // destructor.
  RenderWidgetHostViewCocoa* cocoa_view_;

  // Indicates if the page is loading.
  bool is_loading_;

  // Whether it's allowed to pause waiting for a new frame.
  bool allow_pause_for_resize_or_repaint_;

  // The last scroll offset of the view.
  gfx::Vector2dF last_scroll_offset_;

  // The text to be shown in the tooltip, supplied by the renderer.
  base::string16 tooltip_text_;

  // True when this view acts as a platform view hack for a
  // RenderWidgetHostViewGuest.
  bool is_guest_view_hack_;

  // Factory used to safely scope delayed calls to ShutdownHost().
  base::WeakPtrFactory<RenderWidgetHostViewMac> weak_factory_;

  // selected text on the renderer.
  std::string selected_text_;

  // The window used for popup widgets.
  base::scoped_nsobject<NSWindow> popup_window_;

  // The fullscreen window used for pepper flash.
  base::scoped_nsobject<NSWindow> pepper_fullscreen_window_;
  base::scoped_nsobject<FullscreenWindowManager> fullscreen_window_manager_;
  // Our parent host view, if this is fullscreen.  NULL otherwise.
  RenderWidgetHostViewMac* fullscreen_parent_host_view_;

  // Display link for getting vsync info.
  scoped_refptr<DisplayLinkMac> display_link_;

  // The current VSync timebase and interval. This is zero until the first call
  // to SendVSyncParametersToRenderer(), and refreshed regularly thereafter.
  base::TimeTicks vsync_timebase_;
  base::TimeDelta vsync_interval_;

  // The current composition character range and its bounds.
  gfx::Range composition_range_;
  std::vector<gfx::Rect> composition_bounds_;

  // The current caret bounds.
  gfx::Rect caret_rect_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewMac);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_MAC_H_
