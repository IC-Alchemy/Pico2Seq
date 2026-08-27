// TileButton.h — hub-side edge state for one button behind an Alchemy tile
// ---------------------------------------------------------------------------
// The tile has already debounced its buttons and latched press/release edges
// into sticky bits that a read cannot lose, so this class does not debounce
// again. It turns "level + sticky edges, sampled every poll" into the
// press/hold/tap vocabulary every control surface in this repo already speaks
// (see EncoderGardenEngine's ButtonEvents for the direct-GPIO equivalent).
//
// The sticky bits are folded in so that a press-and-release that lands entirely
// between two polls still registers as a press: level || stickyPressed is
// pressed for at least the poll that carried the sticky bit.
//
// Pure C++ (milliseconds arrive as arguments), so the host suite drives it
// without hardware.

#ifndef ALCHEMY_UI_TILE_BUTTON_H
#define ALCHEMY_UI_TILE_BUTTON_H

#include <cstdint>

class TileButton {
 public:
  struct Options {
    std::uint32_t holdMilliseconds = 400;  // press -> longPress()
  };

  void begin(const Options& options, std::uint32_t nowMilliseconds) {
    opt_ = options;
    held_ = false;
    spent_ = false;
    pressEdge_ = false;
    longPress_ = false;
    releaseTap_ = false;
    pressedAtMilliseconds_ = nowMilliseconds;
  }

  /**
   * Feed one poll of this button's tile frame data. Call once per update()
   * of the owning driver, with the tile's level bitmap bit and the sticky
   * pressed/released bits for this button.
   */
  void update(bool levelPressed, bool stickyPressed, bool stickyReleased,
              std::uint32_t nowMilliseconds) {
    pressEdge_ = false;
    longPress_ = false;
    releaseTap_ = false;

    const bool effectivePressed = levelPressed || stickyPressed;
    if (effectivePressed && !held_) {
      held_ = true;
      spent_ = false;
      pressedAtMilliseconds_ = nowMilliseconds;
      pressEdge_ = true;
      return;
    }

    const bool effectiveReleased = !levelPressed && (stickyReleased || held_);
    if (effectiveReleased && held_) {
      held_ = false;
      releaseTap_ = !spent_;
      return;
    }

    if (!held_ || spent_) return;
    if (nowMilliseconds - pressedAtMilliseconds_ >= opt_.holdMilliseconds) {
      longPress_ = true;
      spent_ = true;
    }
  }

  /** True on the update that saw the press begin. */
  [[nodiscard]] bool pressEdge() const { return pressEdge_; }
  /** True once, holdMilliseconds into a sustained press. */
  [[nodiscard]] bool longPress() const { return longPress_; }
  /** True on release if the press was not spent on a long-press or consume(). */
  [[nodiscard]] bool releaseTap() const { return releaseTap_; }
  /** Current pressed level. */
  [[nodiscard]] bool held() const { return held_; }
  /** How long the current press has lasted (0 when not pressed). */
  [[nodiscard]] std::uint32_t heldMilliseconds(std::uint32_t nowMilliseconds) const {
    return held_ ? nowMilliseconds - pressedAtMilliseconds_ : 0;
  }
  /** Mark the current press as used, suppressing its releaseTap(). */
  void consume() { spent_ = true; }

 private:
  Options opt_{};
  bool held_ = false;
  bool spent_ = false;
  bool pressEdge_ = false;
  bool longPress_ = false;
  bool releaseTap_ = false;
  std::uint32_t pressedAtMilliseconds_ = 0;
};

#endif  // ALCHEMY_UI_TILE_BUTTON_H
