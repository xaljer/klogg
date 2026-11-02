<script lang="ts">
  // Native picker plus a text box that accepts #rrggbb, #rgb or QColor style
  // #aarrggbb (alpha dropped). Text commits on complete input, Enter or blur,
  // and reverts to the current colour on Escape.
  import { createEventDispatcher } from 'svelte';
  import { normalizeHexColor } from '../lib/color';

  export let value = '#ffffff';
  export let disabled = false;
  export let title = '';
  export let hexTitle = '';

  const dispatch = createEventDispatcher<{ change: string }>();

  let text = value;
  let knownValue = value;
  let invalid = false;

  $: if (value !== knownValue) {
    knownValue = value;
    text = value;
    invalid = false;
  }

  function digitsOf(next: string): string {
    return next.trim().replace(/^#/, '');
  }

  function isPartialHex(next: string): boolean {
    const digits = digitsOf(next);
    return digits.length > 0 && digits.length < 6 && /^[0-9a-f]+$/i.test(digits);
  }

  function apply(next: string): void {
    const normalized = normalizeHexColor(next);
    if (!normalized) {
      return;
    }
    invalid = false;
    text = normalized;
    if (normalized !== value) {
      value = normalized;
      knownValue = normalized;
      dispatch('change', normalized);
    }
  }

  function onType(event: Event): void {
    text = (event.currentTarget as HTMLInputElement).value;
    const normalized = normalizeHexColor(text);
    const digits = digitsOf(text);
    if (normalized && (digits.length === 6 || digits.length === 8)) {
      apply(text);
      return;
    }
    invalid = normalized === null && text.trim() !== '' && !isPartialHex(text);
  }

  function onSubmit(event: Event): void {
    const normalized = normalizeHexColor(text);
    if (normalized) {
      apply(text);
    } else {
      text = value;
      invalid = false;
    }
    (event.currentTarget as HTMLInputElement).blur();
  }

  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Enter') {
      onSubmit(event);
    } else if (event.key === 'Escape') {
      text = value;
      invalid = false;
      (event.currentTarget as HTMLInputElement).blur();
    }
  }
</script>

<span class="color-field">
  <input
    class="color-input"
    type="color"
    {value}
    {disabled}
    title={title}
    aria-label={title}
    on:input={(event) => apply(event.currentTarget.value)}
  />
  <input
    class="color-hex"
    class:invalid
    type="text"
    spellcheck="false"
    autocapitalize="off"
    autocomplete="off"
    {disabled}
    value={text}
    title={hexTitle || title}
    aria-label={hexTitle || title}
    on:input={onType}
    on:blur={onSubmit}
    on:keydown={onKeydown}
  />
</span>
