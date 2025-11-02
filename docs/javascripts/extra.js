// Custom JavaScript for NOMAD DAW Documentation

document.addEventListener('DOMContentLoaded', function() {
  // Add smooth scrolling for anchor links
  document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
      const target = document.querySelector(this.getAttribute('href'));
      if (target) {
        e.preventDefault();
        target.scrollIntoView({
          behavior: 'smooth',
          block: 'start'
        });
      }
    });
  });

  // Add copy button feedback
  document.querySelectorAll('.md-clipboard').forEach(button => {
    button.addEventListener('click', function() {
      const icon = this.querySelector('.md-clipboard__icon');
      if (icon) {
        icon.textContent = '✓';
        setTimeout(() => {
          icon.textContent = '';
        }, 2000);
      }
    });
  });

  // Enhance external links
  document.querySelectorAll('a[href^="http"]').forEach(link => {
    if (!link.hostname.includes('currentsuspect.github.io') && 
        !link.hostname.includes('github.com/currentsuspect')) {
      link.setAttribute('target', '_blank');
      link.setAttribute('rel', 'noopener noreferrer');
    }
  });

  // Add loading animation for images
  document.querySelectorAll('img').forEach(img => {
    img.addEventListener('load', function() {
      this.style.opacity = '1';
    });
    img.style.opacity = '0';
    img.style.transition = 'opacity 0.3s ease-in';
  });

  // Initialize any custom components
  initFeatureCards();
  initStatusIcons();
});

// Feature cards interactivity
function initFeatureCards() {
  const cards = document.querySelectorAll('.feature-card');
  cards.forEach(card => {
    card.addEventListener('mouseenter', function() {
      this.style.transform = 'translateY(-4px)';
    });
    card.addEventListener('mouseleave', function() {
      this.style.transform = 'translateY(0)';
    });
  });
}

// Status icons animation
function initStatusIcons() {
  const icons = document.querySelectorAll('.status-icon');
  icons.forEach(icon => {
    icon.style.animation = 'pulse 2s infinite';
  });
}

// Add CSS animation for pulse effect
const style = document.createElement('style');
style.textContent = `
  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.6; }
  }
`;
document.head.appendChild(style);

// Console message for developers
console.log('%c🧭 NOMAD DAW Documentation', 'font-size: 24px; font-weight: bold; color: #5c6bc0;');
console.log('%cBuilt with intention. Create like silence is watching.', 'font-size: 14px; color: #00d9ff;');
console.log('%cSource: https://github.com/currentsuspect/NOMAD', 'font-size: 12px; color: #888;');
