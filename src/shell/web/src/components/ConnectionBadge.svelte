<script lang="ts">
  import { t } from '../lib/i18n';
  import type { StreamStatus } from '../lib/sse';

  export let connection: StreamStatus = 'connecting';

  $: text =
    connection === 'online'
      ? $t('toolbar.connected')
      : connection === 'reconnecting'
        ? $t('toolbar.reconnecting')
        : connection === 'closed'
          ? $t('toolbar.offline')
          : $t('toolbar.connecting');
</script>

<span class="connection" title={text}>
  <span
    class="dot"
    class:online={connection === 'online'}
    class:reconnecting={connection === 'reconnecting'}
    class:offline={connection === 'closed'}
  ></span>
  {text}
</span>
