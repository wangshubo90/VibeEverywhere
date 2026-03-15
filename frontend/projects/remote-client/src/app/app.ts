import { Component } from '@angular/core';
import { FormsModule } from '@angular/forms';
import {
  OpenSessionTabView,
  REMOTE_SAMPLE_SESSIONS,
  REMOTE_SORT_MODES,
  RemoteSessionSortMode,
  SAMPLE_OPEN_TABS,
  SessionSummaryView,
} from '../../../session-model/src/public-api';
import { attentionTone, inventoryTone } from '../../../shared-ui/src/public-api';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [FormsModule],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App {
  readonly sessions: SessionSummaryView[] = REMOTE_SAMPLE_SESSIONS;
  readonly openTabs: OpenSessionTabView[] = SAMPLE_OPEN_TABS;
  readonly sortModes = REMOTE_SORT_MODES;
  sortMode: RemoteSessionSortMode = 'attention';
  activeTabSessionId = this.openTabs[0]?.sessionId ?? '';

  protected inventoryTone = inventoryTone;
  protected attentionTone = attentionTone;
}
