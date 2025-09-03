# Implementation Plan

- [x] 1. Create core hardware detection infrastructure




  - Create base hardware module interface and registry classes
  - Implement thread-safe hardware status storage
  - _Requirements: 1.1, 1.2, 4.1, 4.2_

- [x] 1.1 Implement IHardwareModule interface


  - Write abstract base class with detection, initialization, and status methods
  - Define module identification and capability constants
  - Create fallback support interface methods
  - _Requirements: 1.1, 4.1, 4.2_

- [x] 1.2 Create HardwareRegistry class


  - Implement thread-safe module status storage with mutex protection
  - Write status query methods for runtime hardware checks
  - Create atomic update methods for cross-core safety
  - _Requirements: 1.2, 1.3, 4.2_


- [x] 1.3 Build HardwareDetectionManager core

  - Implement module registration and startup detection orchestration
  - Create health check scheduling and failure handling logic
  - Write UI integration methods for status updates
  - _Requirements: 1.1, 1.2, 1.5, 4.3_

- [x] 2. Implement hardware module detection classes





  - Create concrete module classes for each hardware component
  - Implement I2C-based detection methods with proper error handling
  - Write module-specific initialization and health check logic
  - _Requirements: 1.1, 1.2, 1.3, 5.1, 5.2_

- [x] 2.1 Create TouchMatrixModule class


  - Implement MPR121 detection via I2C communication test
  - Write initialization method with device ID verification
  - Create health check method for runtime monitoring
  - _Requirements: 1.1, 1.2, 5.1_

- [x] 2.2 Create DistanceSensorModule class


  - Implement VL53L1X detection via I2C sensor ID verification
  - Write initialization method with sensor configuration
  - Create health check method with communication timeout detection
  - _Requirements: 1.1, 1.2, 5.1_

- [x] 2.3 Create MagneticEncoderModule class


  - Implement AS5600 detection via I2C register read verification
  - Write initialization method with encoder configuration
  - Create health check method for runtime status monitoring
  - _Requirements: 1.1, 1.2, 5.1_

- [x] 2.4 Create OLEDDisplayModule class


  - Implement SH110X detection via I2C display initialization test
  - Write initialization method with display configuration
  - Create health check method for display communication monitoring
  - _Requirements: 1.1, 1.2, 5.1_
-

- [x] 3. Build fallback control system



  - Create fallback manager to handle alternative control methods
  - Implement input routing for fallback mechanisms
  - Write activation/deactivation logic for seamless transitions
  - _Requirements: 2.1, 2.2, 2.3, 2.4_

- [x] 3.1 Implement FallbackControlManager class


  - Create fallback configuration storage and management
  - Write activation methods for each fallback type
  - Implement input event routing between hardware and fallback controls
  - _Requirements: 2.1, 2.2, 2.3, 2.4_

- [x] 3.2 Create TouchMatrixFallback implementation


  - Implement 4-button digital input fallback for MPR121
  - Write button debouncing and event generation logic
  - Create mapping from button presses to touch matrix events
  - _Requirements: 2.1, 2.4_

- [x] 3.3 Create DistanceSensorFallback implementation


  - Implement step button + AS5600 encoder fallback for VL53L1X
  - Write hold-and-rotate logic for parameter modification
  - Create distance value simulation from encoder input
  - _Requirements: 2.2, 2.4_







- [ ] 3.4 Create MagneticEncoderFallback implementation
  - Implement increment/decrement button fallback for AS5600
  - Write button-based parameter adjustment logic
  - Create encoder value simulation from button presses
  - _Requirements: 2.1, 2.4_



- [ ] 4. Integrate with existing UI and display systems

  - Extend UIState to include hardware status information
  - Update OLED display to show hardware configuration


  - Modify existing sensor code to use detection system
  - _Requirements: 3.1, 3.2, 3.3, 3.4_

- [x] 4.1 Extend UIState with hardware status


  - Add HardwareStatus struct to UIState
  - Create hardware status display toggle functionality
  - Write status message generation methods
  - _Requirements: 3.1, 3.2, 3.3_

- [ ] 4.2 Update OLED display for hardware status
  - Add hardware status display mode to OLEDDisplay class
  - Create visual indicators for module presence and fallback status
  - Implement status message display with scrolling for long messages
  - _Requirements: 3.1, 3.2, 3.3_

- [ ] 4.3 Modify existing sensor initialization code
  - Update Pico2Seq.ino setup1() to use HardwareDetectionManager
  -set up fallback gpio inputs if no mpr121 at compile time
  - Replace direct sensor initialization with detection system calls
  - Add hardware status reporting to Serial output
  - _Requirements: 1.1, 1.2, 3.4_


