<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { t } from '../lib/i18n';

  export let initial = '';

  const dispatch = createEventDispatcher<{
    open: string;
    close: void;
  }>();

  let value = initial;
  let error = '';

  function submit(): void {
    const trimmed = value.trim();
    if (!trimmed) {
      error = $t('location.required');
      return;
    }
    if (/^https?:\/\//i.test(trimmed) && trimmed.toLowerCase().startsWith('https://')) {
      // The server build has no TLS client; keep the message actionable.
      error = $t('location.https');
      return;
    }
    dispatch('open', trimmed);
  }

</script>

<div class="modal-backdrop" role="presentation" on:click={() => dispatch('close')}>
  <div
    class="modal"
    role="dialog"
    aria-modal="true"
    aria-label={$t('location.title')}
    tabindex="-1"
    on:click|stopPropagation
    on:keydown|stopPropagation
  >
    <div class="modal-title">{$t('location.title')}</div>
    <p class="modal-hint">
      {$t('location.hintPrefix')} <code>D:\logs\app.log</code>{$t('location.hintSuffix')}
    </p>
    <input
      class="location-input"
      type="text"
      spellcheck="false"
      placeholder={$t('location.placeholder')}
      bind:value={value}
      on:keydown={(event) => event.key === 'Enter' && submit()}
    />
    {#if error}
      <div class="modal-error">{error}</div>
    {/if}
    <div class="modal-actions">
      <span class="modal-spacer"></span>
      <button class="ghost" on:click={() => dispatch('close')}>{$t('common.cancel')}</button>
      <button class="primary" on:click={submit}>{$t('common.open')}</button>
    </div>
  </div>
</div>
