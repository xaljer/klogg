<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';

  export let x = 0;
  export let y = 0;
  export let items: {
    id: string;
    label: string;
    shortcut?: string;
    disabled?: boolean;
    separator?: boolean;
    checked?: boolean;
    swatch?: string;
    children?: {
      id: string;
      label: string;
      shortcut?: string;
      disabled?: boolean;
      separator?: boolean;
      checked?: boolean;
      swatch?: string;
    }[];
    grid?: {
      id: string;
      label: string;
      short?: string;
      swatch?: string;
      color?: string;
      checked?: boolean;
      disabled?: boolean;
    }[];
  }[] = [];

  const dispatch = createEventDispatcher<{ select: string; close: void }>();

  let menuEl: HTMLDivElement;
  let openSubmenu: string | null = null;
  let submenuUp = false;
  let submenuFlip = false;

  function positionSubmenu(event: Event): void {
    const host = (event.currentTarget as HTMLElement).closest('.context-submenu-host');
    const rect = host?.getBoundingClientRect();
    if (rect) {
      submenuUp = rect.top + 360 > window.innerHeight;
      submenuFlip = rect.right + 210 > window.innerWidth;
    }
  }

  function hoverSubmenu(id: string, event: Event): void {
    positionSubmenu(event);
    openSubmenu = id;
  }

  function toggleSubmenu(id: string, event: Event): void {
    positionSubmenu(event);
    openSubmenu = openSubmenu === id ? null : id;
  }

  function onPointerDown(event: PointerEvent): void {
    if (menuEl && !menuEl.contains(event.target as Node)) {
      dispatch('close');
    }
  }

  function onKeyDown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      dispatch('close');
    }
  }

  onMount(() => {
    // Popover puts the menu in the top layer, above the xterm WebGL canvas.
    try {
      if ('showPopover' in menuEl) {
        menuEl.showPopover();
      }
    } catch {
      // Browser without the popover API: the fixed element is still usable.
    }
    window.addEventListener('pointerdown', onPointerDown, true);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      try {
        if ('hidePopover' in menuEl) {
          menuEl.hidePopover();
        }
      } catch {
        // already closed by the browser
      }
      window.removeEventListener('pointerdown', onPointerDown, true);
      window.removeEventListener('keydown', onKeyDown);
    };
  });
</script>

<div
  class="context-menu"
  bind:this={menuEl}
  style={`left:${x}px;top:${y}px`}
  role="menu"
  tabindex="-1"
  popover="manual"
  on:contextmenu|preventDefault
>
  {#each items as item (item.id + item.label)}
    {#if item.separator}
      <div class="context-separator"></div>
    {:else if item.grid?.length}
      <div class="context-grid-block" role="group" aria-label={item.label}>
        <div class="context-grid-title">{item.label}</div>
        <div class="context-grid">
          {#each item.grid as cell (cell.id)}
            <button
              type="button"
              class="context-grid-cell"
              class:checked={cell.checked}
              data-menu-id={cell.id}
              role="menuitemradio"
              aria-checked={cell.checked ?? false}
              aria-label={cell.label}
              title={cell.label}
              disabled={cell.disabled}
              on:click={() => dispatch('select', cell.id)}
            >
              <span
                class="context-grid-chip"
                class:none={!cell.swatch}
                style={cell.swatch
                  ? `background:${cell.swatch};color:${cell.color ?? '#111111'}`
                  : ''}
              >{cell.short ?? ''}</span>
            </button>
          {/each}
        </div>
      </div>
    {:else if item.children?.length}
      <div
        class="context-submenu-host"
        role="none"
        on:mouseenter={(event) => hoverSubmenu(item.id, event)}
        on:mouseleave={() => {
          if (openSubmenu === item.id) {
            openSubmenu = null;
          }
        }}
      >
        <button
          class="context-item has-children"
          class:open={openSubmenu === item.id}
          role="menuitem"
          aria-haspopup="menu"
          aria-expanded={openSubmenu === item.id}
          disabled={item.disabled}
          on:click={(event) => toggleSubmenu(item.id, event)}
        >
          <span class="context-item-label">{item.label}</span>
          <span class="context-shortcut">{item.shortcut ?? '›'}</span>
        </button>
        {#if openSubmenu === item.id}
          <div
            class="context-submenu"
            class:flip={submenuFlip}
            class:up={submenuUp}
            role="menu"
          >
            {#each item.children as child (child.id + child.label)}
              {#if child.separator}
                <div class="context-separator"></div>
              {:else}
                <button
                  class="context-item"
                  class:checked={child.checked}
                  data-menu-id={child.id}
                  role="menuitemradio"
                  aria-checked={child.checked ?? false}
                  disabled={child.disabled}
                  on:click={() => dispatch('select', child.id)}
                >
                  <span class="context-item-label">
                    {#if child.checked}<span class="context-check">✓</span>{/if}
                    {#if child.swatch}
                      <span class="context-swatch" style={`background:${child.swatch}`}></span>
                    {/if}
                    <span>{child.label}</span>
                  </span>
                  {#if child.shortcut}<span class="context-shortcut">{child.shortcut}</span>{/if}
                </button>
              {/if}
            {/each}
          </div>
        {/if}
      </div>
    {:else}
      <button
        class="context-item"
        class:checked={item.checked}
        data-menu-id={item.id}
        role="menuitemradio"
        aria-checked={item.checked ?? false}
        disabled={item.disabled}
        on:click={() => dispatch('select', item.id)}
      >
        <span class="context-item-label">
          {#if item.checked}<span class="context-check">✓</span>{/if}
          {#if item.swatch}
            <span class="context-swatch" style={`background:${item.swatch}`}></span>
          {/if}
          <span>{item.label}</span>
        </span>
        {#if item.shortcut}<span class="context-shortcut">{item.shortcut}</span>{/if}
      </button>
    {/if}
  {/each}
</div>
