/**
 * fsmc Playground — Viewport Controller
 * Manages pan, pinch-zoom, scroll-zoom, and keyboard zoom on the canvas SVG.
 */

import { ModelManager } from './model_manager.js';

export const ViewportController = {
  init() {
    const canvas = document.getElementById("mermaidCanvas");
    let isDragging = false, startX = 0, startY = 0;
    let initialTouchDist = 0, initialZoom = 1.0;

    canvas.onmousedown = (e) => {
      if (e.target.closest('.node')) return;
      isDragging = true;
      startX = e.clientX - ModelManager.currentModel.panX;
      startY = e.clientY - ModelManager.currentModel.panY;
      canvas.style.cursor = "grabbing";
    };

    window.onmousemove = (e) => {
      if (!isDragging) return;
      ModelManager.currentModel.panX = e.clientX - startX;
      ModelManager.currentModel.panY = e.clientY - startY;
      this.applyTransform();
    };

    window.onmouseup = () => {
      if (isDragging) { isDragging = false; canvas.style.cursor = "grab"; }
    };

    canvas.addEventListener("touchstart", (e) => {
      if (e.target.closest('.node')) return;
      if (e.touches.length === 1) {
        isDragging = true;
        startX = e.touches[0].clientX - ModelManager.currentModel.panX;
        startY = e.touches[0].clientY - ModelManager.currentModel.panY;
      } else if (e.touches.length === 2) {
        isDragging = false;
        initialTouchDist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        initialZoom = ModelManager.currentModel.zoom;
      }
    }, { passive: true });

    canvas.addEventListener("touchmove", (e) => {
      if (isDragging && e.touches.length === 1) {
        ModelManager.currentModel.panX = e.touches[0].clientX - startX;
        ModelManager.currentModel.panY = e.touches[0].clientY - startY;
        this.applyTransform();
      } else if (e.touches.length === 2 && initialTouchDist > 0) {
        const currentDist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        ModelManager.currentModel.zoom = Math.max(0.2, Math.min(3.0, initialZoom * (currentDist / initialTouchDist)));
        this.applyTransform();
        const badge = document.getElementById("zoomLevelBadge");
        if (badge) badge.textContent = `${Math.round(ModelManager.currentModel.zoom * 100)}%`;
      }
    }, { passive: true });

    canvas.addEventListener("touchend", () => {
      isDragging = false;
      initialTouchDist = 0;
    }, { passive: true });

    canvas.onwheel = (e) => {
      e.preventDefault();
      this.zoom(e.deltaY < 0 ? 1.1 : 0.9);
    };

    const zoomInBtn  = document.getElementById("zoomInBtn");
    const zoomOutBtn = document.getElementById("zoomOutBtn");
    const resetBtn   = document.getElementById("zoomResetBtn");
    if (zoomInBtn)  zoomInBtn.onclick  = () => this.zoom(1.15);
    if (zoomOutBtn) zoomOutBtn.onclick = () => this.zoom(0.85);
    if (resetBtn)   resetBtn.onclick   = () => this.reset();
  },

  zoom(factor) {
    ModelManager.currentModel.zoom = Math.max(0.2, Math.min(3.0, ModelManager.currentModel.zoom * factor));
    this.applyTransform();
    const badge = document.getElementById("zoomLevelBadge");
    if (badge) badge.textContent = `${Math.round(ModelManager.currentModel.zoom * 100)}%`;
  },

  reset() {
    ModelManager.currentModel.zoom = 1.0;
    ModelManager.currentModel.panX = 0;
    ModelManager.currentModel.panY = 0;
    this.applyTransform();
    const badge = document.getElementById("zoomLevelBadge");
    if (badge) badge.textContent = "100%";
  },

  applyTransform(smooth = false) {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;
    svg.style.transition   = smooth ? "transform 0.2s ease-out" : "none";
    svg.style.transform    = `translate(${ModelManager.currentModel.panX}px, ${ModelManager.currentModel.panY}px) scale(${ModelManager.currentModel.zoom})`;
    svg.style.transformOrigin = "center center";
  }
};
