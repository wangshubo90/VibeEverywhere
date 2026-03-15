import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SessionModel } from './session-model';

describe('SessionModel', () => {
  let component: SessionModel;
  let fixture: ComponentFixture<SessionModel>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [SessionModel]
    })
    .compileComponents();

    fixture = TestBed.createComponent(SessionModel);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
