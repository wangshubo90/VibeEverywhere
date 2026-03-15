export type BadgeTone = 'neutral' | 'good' | 'warn' | 'danger' | 'muted';
export type InventoryToneState = 'live' | 'ended' | 'archived';
export type AttentionToneState = 'none' | 'info' | 'action_required' | 'intervention';

export function inventoryTone(state: InventoryToneState): BadgeTone {
  switch (state) {
    case 'live':
      return 'good';
    case 'archived':
      return 'warn';
    case 'ended':
    default:
      return 'muted';
  }
}

export function attentionTone(state: AttentionToneState): BadgeTone {
  switch (state) {
    case 'action_required':
      return 'warn';
    case 'intervention':
      return 'danger';
    case 'info':
      return 'good';
    case 'none':
    default:
      return 'muted';
  }
}
