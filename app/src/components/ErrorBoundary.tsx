//
// ErrorBoundary — keeps one screen's runtime error from blanking the whole app.
// Wrapped around the routed <Outlet/> in Layout, so a crash shows a friendly
// retry inside the content area while the header + tab bar stay usable. Pass the
// current pathname as `resetKey` so navigating to another screen clears it.
//
import { Component, type ReactNode } from 'react';
import { EmptyState } from './ui';

interface Props {
  children: ReactNode;
  resetKey?: string;
}
interface State {
  error: Error | null;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidUpdate(prev: Props) {
    // Navigating to a different screen clears a previously-crashed one.
    if (prev.resetKey !== this.props.resetKey && this.state.error) {
      this.setState({ error: null });
    }
  }

  render() {
    if (this.state.error) {
      return (
        <div style={{ padding: 24 }}>
          <EmptyState
            icon={<span aria-hidden>⚠️</span>}
            title="This screen hit a snag"
            hint={
              this.state.error.message ||
              'Something went wrong rendering this view. Other screens still work.'
            }
          />
          <div style={{ textAlign: 'center', marginTop: 12 }}>
            <button className="btn" onClick={() => this.setState({ error: null })}>
              Try again
            </button>
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}
